const std = @import("std");

// Parallel tiny-regex-c implementation; see LICENSE.tiny-regex-c. Preserve
// the repository's dialect, with bounded parsing and caller-owned storage.
const Kind = enum(u8) { unused, dot, begin, end, question, star, plus, char, class, inverse, digit, not_digit, alpha, not_alpha, whitespace, not_whitespace };
const Token = struct { kind: Kind = .unused, char: u8 = 0, class_start: u8 = 0, bits: [4]u64 = @splat(0) };
pub const Pattern = struct { tokens: [30]Token = @splat(.{}), classes: [40]u8 = @splat(0), len: usize = 0 };
const ParseError = error{ InvalidPattern, PatternTooLong, ClassTooLong };
const MatchError = error{ WorkLimit };

fn digit(c: u8) bool { return c >= '0' and c <= '9'; }
fn alpha(c: u8) bool { return (c >= 'a' and c <= 'z') or (c >= 'A' and c <= 'Z') or digit(c) or c == '_'; }
fn space(c: u8) bool { return c == ' ' or (c >= 9 and c <= 13); }
fn meta(c: u8, code: u8) bool {
    return switch (code) { 'd' => digit(c), 'D' => !digit(c), 'w' => alpha(c), 'W' => !alpha(c), 's' => space(c), 'S' => !space(c), else => c == code };
}
fn isMeta(c: u8) bool { return c == 'd' or c == 'D' or c == 'w' or c == 'W' or c == 's' or c == 'S'; }

// Preserve the original character-class/range rules, including '-'.
fn inClass(c: u8, chars: []const u8, start: usize) bool {
    var i = start;
    while (i < chars.len) : (i += 1) {
        const ch = chars[i];
        if (c != '-' and ch != 0 and ch != '-' and i + 2 < chars.len and chars[i + 1] == '-' and chars[i + 2] != 0 and c >= ch and c <= chars[i + 2]) return true;
        if (ch == '\\') {
            i += 1;
            if (i >= chars.len) return false;
            if (meta(c, chars[i]) or (c == chars[i] and !isMeta(chars[i]))) return true;
        } else if (c == ch) {
            if (c == '-') return chars[i - 1] == 0 or (i + 1 < chars.len and chars[i + 1] == 0);
            return true;
        }
        if (chars[i] == 0) break;
    }
    return false;
}

pub fn compile(source: []const u8) ParseError!Pattern {
    var out: Pattern = .{};
    var i: usize = 0;
    var used: usize = 1;
    while (i < source.len) : (i += 1) {
        if (out.len >= 29) return error.PatternTooLong;
        const ch = source[i];
        if (ch == 0) return error.InvalidPattern;
        var token: Token = .{};
        switch (ch) {
            '^' => { if (out.len != 0) return error.InvalidPattern; token.kind = .begin; },
            '$' => { if (i + 1 != source.len) return error.InvalidPattern; token.kind = .end; },
            '.' => token.kind = .dot,
            '*', '+', '?' => {
                if (out.len == 0) return error.InvalidPattern;
                switch (out.tokens[out.len - 1].kind) { .begin, .end, .star, .plus, .question => return error.InvalidPattern, else => {} }
                token.kind = switch (ch) { '*' => .star, '+' => .plus, else => .question };
            },
            '\\' => {
                i += 1;
                if (i == source.len or source[i] == 0) return error.InvalidPattern;
                token.kind = switch (source[i]) { 'd' => .digit, 'D' => .not_digit, 'w' => .alpha, 'W' => .not_alpha, 's' => .whitespace, 'S' => .not_whitespace, else => .char };
                token.char = source[i];
            },
            '[' => {
                token.kind = .class;
                if (i + 1 < source.len and source[i + 1] == '^') { token.kind = .inverse; i += 1; }
                token.class_start = @intCast(used);
                i += 1;
                while (i < source.len and source[i] != ']') : (i += 1) {
                    if (source[i] == 0 or used >= 39) return error.ClassTooLong;
                    if (source[i] == '\\') {
                        if (i + 1 >= source.len or used >= 38) return error.InvalidPattern;
                        out.classes[used] = source[i]; used += 1; i += 1;
                    }
                    out.classes[used] = source[i]; used += 1;
                }
                if (i == source.len or used >= 40) return error.InvalidPattern;
                out.classes[used] = 0; used += 1;
                for (0..256) |value| {
                    const yes = inClass(@intCast(value), &out.classes, token.class_start);
                    if (yes != (token.kind == .inverse)) token.bits[value / 64] |= @as(u64, 1) << @intCast(value % 64);
                }
            },
            else => { token.kind = .char; token.char = ch; },
        }
        out.tokens[out.len] = token;
        out.len += 1;
    }
    return out;
}

fn one(pattern: *const Pattern, token: Token, ch: u8, optimized: bool) bool {
    return switch (token.kind) {
        // re.h defines RE_DOT_MATCHES_NEWLINE=1 in the shipped sample.
        .dot => true,
        .char => ch == token.char,
        .class, .inverse => if (optimized) (token.bits[ch / 64] & (@as(u64, 1) << @intCast(ch % 64))) != 0 else inClass(ch, &pattern.classes, token.class_start) != (token.kind == .inverse),
        .digit => digit(ch), .not_digit => !digit(ch),
        .alpha => alpha(ch), .not_alpha => !alpha(ch),
        .whitespace => space(ch), .not_whitespace => !space(ch),
        else => false,
    };
}
fn spend(budget: *usize) MatchError!void {
    if (budget.* == 0) return error.WorkLimit;
    budget.* -= 1;
}

// Mirror the C accumulator updates: failed matches can leave nonzero length.
fn match(pattern: *const Pattern, first: usize, text: []const u8, start: usize, length: *c_int, budget: *usize, optimized: bool) MatchError!bool {
    var p = first;
    var t = start;
    const before = length.*;
    while (true) {
        try spend(budget);
        const token = pattern.tokens[p];
        if (token.kind == .unused) return true;
        const next = if (p + 1 < pattern.tokens.len) pattern.tokens[p + 1].kind else Kind.unused;
        if (next == .question) {
            if (try match(pattern, p + 2, text, t, length, budget, optimized)) return true;
            if (t < text.len and one(pattern, token, text[t], optimized) and try match(pattern, p + 2, text, t + 1, length, budget, optimized)) {
                length.* += 1; return true;
            }
            return false;
        }
        if (next == .star or next == .plus) {
            const prelen = length.*;
            var end = t;
            while (end < text.len and one(pattern, token, text[end], optimized)) : (end += 1) {
                try spend(budget); length.* += 1;
            }
            const minimum = t + @intFromBool(next == .plus);
            while (end >= minimum) {
                if (try match(pattern, p + 2, text, end, length, budget, optimized)) return true;
                length.* -= 1;
                if (end == minimum) break;
                end -= 1;
            }
            if (next == .star) length.* = prelen;
            return false;
        }
        if (token.kind == .end and next == .unused) return t == text.len;
        length.* += 1;
        if (t >= text.len or !one(pattern, token, text[t], optimized)) {
            length.* = before; return false;
        }
        p += 1; t += 1;
    }
}

fn findByte(text: []const u8, needle: u8) ?usize {
    const V = @Vector(16, u8);
    const wanted: V = @splat(needle);
    var i: usize = 0;
    while (text.len - i >= 16) : (i += 16) {
        const bytes: V = text[i..][0..16].*;
        const bits: u16 = @bitCast(bytes == wanted);
        if (bits != 0) return i + @as(usize, @ctz(bits));
    }
    for (text[i..], i..) |ch, position| if (ch == needle) return position;
    return null;
}

pub fn search(pattern: *const Pattern, text: []const u8, length: *c_int, optimized: bool, work_limit: usize) MatchError!c_int {
    length.* = 0;
    var budget = work_limit;
    if (pattern.tokens[0].kind == .begin) return if (try match(pattern, 1, text, 0, length, &budget, optimized)) 0 else -1;
    const first = pattern.tokens[0];
    const second = pattern.tokens[1].kind;
    const skip = optimized and first.kind == .char and second != .star and second != .question and second != .plus;
    var i: usize = 0;
    while (i <= text.len) : (i += 1) {
        if (skip) i += findByte(text[i..], first.char) orelse return -1;
        if (try match(pattern, 0, text, i, length, &budget, optimized)) return if (i == text.len) -1 else @intCast(i);
    }
    return -1;
}
comptime {
    if (@sizeOf(Pattern) > 2048 or @alignOf(Pattern) > 16) @compileError("Update drbz_regex storage in native.h");
}
export fn drbz_regex_compile(storage: *anyopaque, source: [*c]const u8, len: usize) c_int {
    const input: []const u8 = if (len == 0) "" else source[0..len];
    const pattern = compile(input) catch return 1;
    const out: *Pattern = @ptrCast(@alignCast(storage));
    out.* = pattern;
    return 0;
}
// Search only after successful compile. Binary text is an explicit extension
// beyond the original NUL-terminated interface, not a C compatibility claim.
export fn drbz_regex_search(storage: *const anyopaque, text: [*c]const u8, len: usize, length: *c_int, optimized: c_int, work_limit: usize) c_int {
    if (len > std.math.maxInt(c_int)) { length.* = 0; return -2; }
    const pattern: *const Pattern = @ptrCast(@alignCast(storage));
    const input: []const u8 = if (len == 0) "" else text[0..len];
    return search(pattern, input, length, optimized != 0, work_limit) catch { length.* = 0; return -2; };
}
test "malformed patterns and independent storage" {
    for ([_][]const u8{ "[", "[^", "\\", "*a", "a**", "a^", "$a", "[a\\" }) |source| {
        if (compile(source)) |_| return error.ExpectedInvalidPattern else |_| {}
    }
    try std.testing.expectError(error.PatternTooLong, compile("abcdefghijklmnopqrstuvwxyzabcd"));
    const a = try compile("alpha");
    const b = try compile("beta");
    var length: c_int = 0;
    try std.testing.expectEqual(@as(c_int, 0), try search(&a, "alpha beta", &length, true, 1000));
    try std.testing.expectEqual(@as(c_int, 6), try search(&b, "alpha beta", &length, true, 1000));
}
test "classes, non-greedy question and work limit" {
    var length: c_int = 0;
    const p = try compile("[a-z]+\\d?");
    try std.testing.expectEqual(@as(c_int, 3), try search(&p, "123abc7", &length, true, 10000));
    try std.testing.expectEqual(@as(c_int, 3), length);
    const q = try compile("a*a*a*a*a*b");
    try std.testing.expectError(error.WorkLimit, search(&q, "aaaaaaaaaaaaaaaa", &length, false, 32));
    for (0..64) |offset| {
        var text: [100]u8 = @splat('x');
        text[offset] = 'z';
        try std.testing.expectEqual(@as(?usize, offset), findByte(&text, 'z'));
    }
}
