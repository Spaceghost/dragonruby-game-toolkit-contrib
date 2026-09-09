"""Adversarial reporter fixtures: invalid evidence must fail closed."""
import hashlib
import json
import unittest
import report
import measure


def records(scope='sqlite', trials=11):
    return [{'case': case, 'variant': variant, 'size': spec[1], 'iterations': spec[2],
             'trial': trial, 'nanoseconds': 10000 + trial, 'checksum': 12345, 'allocation_count': None}
            for case, spec in report.CONTRACTS[scope].items() for variant in spec[0] for trial in range(trials)]


def log(rows, scope='sqlite', **changes):
    metadata = {'schema': 1, 'scope': scope, 'trials': 11, 'commit': 'a' * 40,
                'source_sha256': {'fixture': 'b' * 64}, 'command': ['zig', 'build', 'bench'],
                'zig': '0.16.0', 'cpu_target': 'baseline', 'build_mode': 'ReleaseFast',
                'os': 'test-fixture', 'machine': 'test-fixture'}
    metadata.update(changes)
    payload = ''.join('BENCH ' + json.dumps(row) + '\n' for row in rows)
    footer = {'exit_code': 0, 'sources_unchanged': True,
              'output_sha256': hashlib.sha256(payload.encode()).hexdigest()}
    return 'BENCH_ENV ' + json.dumps(metadata) + '\n' + payload + 'BENCH_RUN ' + json.dumps(footer) + '\n'


class ReportTests(unittest.TestCase):
    def test_complete_sqlite(self):
        metadata, groups = report.validate(log(records()))
        self.assertEqual(len(groups), 4)
        summaries = report.summarize(metadata, groups)
        reuse = next(row for row in summaries if row['variant'] == 'zig_reuse')
        self.assertEqual(reuse['reference_variant'], 'c_reuse')
        prepare = next(row for row in summaries if row['variant'] == 'zig_prepare_each')
        self.assertEqual(prepare['reference_variant'], 'c_control')

    def test_complete_kernels(self):
        _, groups = report.validate(log(records('kernels'), 'kernels'))
        self.assertEqual(len(groups), 36)

    def test_invalid_records(self):
        fixtures = {'missing_trial': records()[:-1], 'duplicate_trial': records() + [records()[0]],
                    'missing_variant': [row for row in records() if row['variant'] != 'c_reuse'],
                    'missing_case': [row for row in records('kernels') if row['case'] != 'lf/tiny']}
        for key, value in (('nanoseconds', 0), ('nanoseconds', -1), ('nanoseconds', float('nan')),
                           ('iterations', 0), ('iterations', True), ('trial', 11), ('checksum', -1),
                           ('checksum', 99), ('size', 2), ('allocation_count', 0), ('variant', 'unknown')):
            rows = records()
            rows[0][key] = value
            fixtures[f'{key}:{value}'] = rows
        for name, rows in fixtures.items():
            with self.subTest(name=name), self.assertRaises(ValueError):
                report.validate(log(rows, 'kernels' if name == 'missing_case' else 'sqlite'))

    def test_whole_variant_wrong_checksum(self):
        rows = records()
        for row in rows:
            if row['variant'] == 'zig_reuse':
                row['checksum'] = 777
        with self.assertRaises(ValueError):
            report.validate(log(rows))

    def test_bad_provenance(self):
        original = log(records())
        fixtures = [original.split('\n', 1)[1], original.rsplit('BENCH_RUN ', 1)[0],
                    original.replace('"exit_code": 0', '"exit_code": 1'),
                    original.replace('"sources_unchanged": true', '"sources_unchanged": false'),
                    original.replace('10000', '10001', 1),
                    log(records(), trials=3), log(records(), commit=''), log(records(), schema=2),
                    log(records(), schema=True), log(records(), source_sha256={'x': 'bad'}),
                    log(records(), zig='unrecorded'), log(records(), command='not argv'), log(records(), scope=[])]
        for i, text in enumerate(fixtures):
            with self.subTest(fixture=i), self.assertRaises(ValueError):
                report.validate(text)

    def test_capture_digest_and_failure_footer(self):
        fixture = log(records())
        metadata = json.loads(fixture.splitlines()[0][10:])
        payload = ''.join(fixture.splitlines(keepends=True)[1:-1])
        captured = measure.record(metadata, payload.replace('\n', '\r\n'), 0, True)
        self.assertEqual(len(report.validate(captured)[1]), 4)
        for code, unchanged in ((1, True), (124, True), (0, False)):
            with self.subTest(code=code, unchanged=unchanged), self.assertRaises(ValueError):
                report.validate(measure.record(metadata, payload, code, unchanged))

    def test_report_uses_recorded_machine(self):
        metadata, groups = report.validate(log(records(), machine='recorded-on-another-machine'))
        self.assertEqual(metadata['machine'], 'recorded-on-another-machine')
        self.assertEqual(len(report.summarize(metadata, groups)), 4)


if __name__ == '__main__':
    unittest.main()
