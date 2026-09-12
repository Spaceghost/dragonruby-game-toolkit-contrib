package sqlite_odin

foreign import sqlite "system:sqlite3"
foreign sqlite {
	sqlite3_finalize      :: proc(stmt: rawptr) -> i32 ---
	sqlite3_free          :: proc(memory: rawptr) ---
	sqlite3_malloc64      :: proc(bytes: u64) -> rawptr ---
	sqlite3_realloc64     :: proc(memory: rawptr, bytes: u64) -> rawptr ---
	sqlite3_stmt_busy     :: proc(stmt: rawptr) -> i32 ---
	sqlite3_prepare_v3    :: proc(db: rawptr, sql: [^]u8, bytes: i32, flags: u32, stmt: ^rawptr, tail: rawptr) -> i32 ---
	sqlite3_step          :: proc(stmt: rawptr) -> i32 ---
	sqlite3_column_type   :: proc(stmt: rawptr, column: i32) -> i32 ---
	sqlite3_column_text   :: proc(stmt: rawptr, column: i32) -> rawptr ---
	sqlite3_column_bytes  :: proc(stmt: rawptr, column: i32) -> i32 ---
	sqlite3_reset         :: proc(stmt: rawptr) -> i32 ---
}

SQLITE_OK                 :: i32(0)
SQLITE_NOMEM              :: i32(7)
SQLITE_TOOBIG             :: i32(18)
SQLITE_MISUSE             :: i32(21)
SQLITE_ROW                :: i32(100)
SQLITE_DONE               :: i32(101)
SQLITE_NULL               :: i32(5)
SQLITE_PREPARE_PERSISTENT :: u32(1)
NULL_ROW                  :: max(u64)

Cache :: struct {
	db:         rawptr,
	stmt:       rawptr,
	key:        rawptr,
	key_len:    uintptr,
	buffer:     rawptr,
	capacity:   uintptr,
	output_len: uintptr,
	rows:       uintptr,
	prepares:   u64,
	hits:       u64,
	valid:      i32,
}

Result :: struct {
	code:       i32,
	reset_code: i32,
	rows:       uintptr,
	cache_hit:  i32,
}

when size_of(rawptr) == 8 {
	// Ten pointer/size/u64 fields (80 bytes) plus the trailing C int and its
	// natural 8-byte struct padding. Keep this assertion because the C benchmark
	// passes drbz_query_cache directly across the Odin ABI.
	#assert(size_of(Cache) == 88)
	#assert(size_of(Result) == 24)
}

zero_cache :: proc "contextless" (cache: ^Cache, db: rawptr, prepares, hits: u64) {
	cache^ = Cache{db=db, prepares=prepares, hits=hits}
}

@(export)
drbo_query_cache_init :: proc "c" (cache: ^Cache, db: rawptr) {
	zero_cache(cache, db, 0, 0)
}

@(export)
drbo_query_cache_clear :: proc "c" (cache: ^Cache) -> i32 {
	code := SQLITE_OK
	if cache.stmt != nil { code = sqlite3_finalize(cache.stmt) }
	if cache.key != nil { sqlite3_free(cache.key) }
	if cache.buffer != nil { sqlite3_free(cache.buffer) }
	db, prepares, hits := cache.db, cache.prepares, cache.hits
	zero_cache(cache, db, prepares, hits)
	return code
}

has_zero :: proc "contextless" (bytes: [^]u8, length: uintptr) -> bool #no_bounds_check {
	for i: uintptr = 0; i < length; i += 1 {
		if bytes[i] == 0 { return true }
	}
	return false
}

same_key :: proc "contextless" (cache: ^Cache, sql: [^]u8, length: uintptr) -> bool #no_bounds_check {
	if cache.valid == 0 || cache.key == nil || cache.key_len != length { return false }
	key := cast([^]u8)cache.key
	for i: uintptr = 0; i < length; i += 1 {
		if key[i] != sql[i] { return false }
	}
	return true
}

copy_bytes :: proc "contextless" (destination, source: [^]u8, length: uintptr) #no_bounds_check {
	for i: uintptr = 0; i < length; i += 1 { destination[i] = source[i] }
}

prepare_cached :: proc "contextless" (cache: ^Cache, sql: [^]u8, length: uintptr, hit: ^i32) -> i32 #no_bounds_check {
	hit^ = 0
	if cache.db == nil { return SQLITE_MISUSE }
	if length > uintptr(max(i32)) { return SQLITE_TOOBIG }
	if has_zero(sql, length) { return SQLITE_MISUSE }
	if cache.stmt != nil && sqlite3_stmt_busy(cache.stmt) != 0 { return SQLITE_MISUSE }
	if same_key(cache, sql, length) {
		cache.hits += 1
		hit^ = 1
		return SQLITE_OK
	}

	new_stmt: rawptr
	code := sqlite3_prepare_v3(cache.db, sql, i32(length), SQLITE_PREPARE_PERSISTENT, &new_stmt, nil)
	if code != SQLITE_OK { return code }
	allocation := length
	if allocation == 0 { allocation = 1 }
	new_key := sqlite3_malloc64(u64(allocation))
	if new_key == nil {
		if new_stmt != nil { _ = sqlite3_finalize(new_stmt) }
		return SQLITE_NOMEM
	}
	if length != 0 { copy_bytes(cast([^]u8)new_key, sql, length) }

	old_stmt, old_key := cache.stmt, cache.key
	cache.stmt = new_stmt
	cache.key = new_key
	cache.key_len = length
	cache.valid = 1
	cache.prepares += 1
	cleanup := SQLITE_OK
	if old_stmt != nil { cleanup = sqlite3_finalize(old_stmt) }
	if old_key != nil { sqlite3_free(old_key) }
	return cleanup
}

reserve :: proc "contextless" (cache: ^Cache, needed: uintptr) -> i32 {
	if needed <= cache.capacity { return SQLITE_OK }
	next := cache.capacity
	if next == 0 { next = 256 }
	for next < needed {
		if next > max(uintptr) / 2 {
			next = needed
			break
		}
		next *= 2
	}
	raw := sqlite3_realloc64(cache.buffer, u64(next))
	if raw == nil { return SQLITE_NOMEM }
	cache.buffer = raw
	cache.capacity = next
	return SQLITE_OK
}

append_first :: proc "contextless" (cache: ^Cache, stmt: rawptr) -> i32 #no_bounds_check {
	header := NULL_ROW
	bytes: [^]u8
	length: uintptr = 0
	if sqlite3_column_type(stmt, 0) != SQLITE_NULL {
		raw := sqlite3_column_text(stmt, 0)
		if raw == nil { return SQLITE_NOMEM }
		n := sqlite3_column_bytes(stmt, 0)
		if n < 0 { return SQLITE_TOOBIG }
		length = uintptr(n)
		header = u64(length)
		bytes = cast([^]u8)raw
	}
	if cache.output_len > max(uintptr) - 8 { return SQLITE_TOOBIG }
	with_header := cache.output_len + 8
	if length > max(uintptr) - with_header { return SQLITE_TOOBIG }
	total := with_header + length
	code := reserve(cache, total)
	if code != SQLITE_OK { return code }
	buffer := cast([^]u8)cache.buffer
	header_bytes := transmute([8]u8)header
	for i in 0..<8 { buffer[cache.output_len + uintptr(i)] = header_bytes[i] }
	cache.output_len = with_header
	if length != 0 {
		copy_bytes(buffer[cache.output_len:], bytes, length)
		cache.output_len += length
	}
	cache.rows += 1
	return SQLITE_OK
}

@(export)
drbo_query_pack :: proc "c" (cache: ^Cache, sql: [^]u8, length: uintptr) -> Result #no_bounds_check {
	cache.output_len = 0
	cache.rows = 0
	hit: i32
	code := prepare_cached(cache, sql, length, &hit)
	if code != SQLITE_OK { return Result{code=code, reset_code=SQLITE_OK, cache_hit=hit} }
	if cache.stmt == nil { return Result{code=SQLITE_OK, reset_code=SQLITE_OK, cache_hit=hit} }

	for {
		step := sqlite3_step(cache.stmt)
		if step == SQLITE_DONE { break }
		if step != SQLITE_ROW { code = step; break }
		code = append_first(cache, cache.stmt)
		if code != SQLITE_OK { break }
	}
	completed_rows := cache.rows
	reset_code := sqlite3_reset(cache.stmt)
	if code == SQLITE_OK && reset_code != SQLITE_OK { code = reset_code }
	if code != SQLITE_OK {
		cache.output_len = 0
		cache.rows = 0
	}
	return Result{code=code, reset_code=reset_code, rows=completed_rows, cache_hit=hit}
}

@(export)
drbo_query_output :: proc "c" (cache: ^Cache, data: ^rawptr, length, rows: ^uintptr) {
	data^ = cache.buffer
	length^ = cache.output_len
	rows^ = cache.rows
}
