"""Deliberately damage evidence so a plausible-looking partial report cannot pass."""
import copy
import unittest
from benchmark_evidence import validate, summarize


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.manifest = {'example': ['c', 'zig']}
        self.rows = [{'event': 'case', 'case': 'example', 'variants': ['c', 'zig']}]
        for trial in range(3):
            for order, variant in enumerate(('c', 'zig')):
                self.rows.append({'event': 'timing', 'case': 'example', 'variant': variant,
                                  'trial': trial, 'order': order, 'iterations': 100,
                                  'nanoseconds': 2000 if variant == 'c' else 1000,
                                  'min_ns': 1000, 'undersized': False, 'instrumented': False,
                                  'checksum': 42, 'process': 0})
        self.rows.append({'event': 'complete', 'cases': 1, 'records': 6})

    def test_valid_and_paired_ratio(self):
        self.assertEqual(len(validate(self.rows, self.manifest, 3, 'timing')), 6)
        summary, text = summarize(self.rows, self.manifest)
        self.assertEqual(summary[1]['paired_ratio_median'], 2)
        self.assertIn('p95 batch-mean', text)

    def test_missing_sample(self):
        self.rows.pop(1)
        with self.assertRaises(ValueError): validate(self.rows, self.manifest, 3, 'timing')

    def test_duplicate_sample(self):
        self.rows[2] = copy.deepcopy(self.rows[1])
        with self.assertRaises(ValueError): validate(self.rows, self.manifest, 3, 'timing')

    def test_corrupted_checksum(self):
        self.rows[1]['checksum'] ^= 1
        with self.assertRaises(ValueError): validate(self.rows, self.manifest, 3, 'timing')

    def test_unequal_work(self):
        self.rows[1]['iterations'] *= 2
        with self.assertRaises(ValueError): validate(self.rows, self.manifest, 3, 'timing')

    def test_instrumented_timing(self):
        self.rows[1]['instrumented'] = True
        with self.assertRaises(ValueError): validate(self.rows, self.manifest, 3, 'timing')

    def test_false_duration_label(self):
        self.rows[1]['undersized'] = True
        with self.assertRaises(ValueError): validate(self.rows, self.manifest, 3, 'timing')

    def test_missing_completion(self):
        self.rows.pop()
        with self.assertRaises(ValueError): validate(self.rows, self.manifest, 3, 'timing')

    def test_manifest_not_inferred_from_survivors(self):
        with self.assertRaises(ValueError): validate(self.rows, {'extra': ['c']}, 3, 'timing')

    def test_reject_negative_or_bool_count(self):
        for value in (-1, True):
            rows = copy.deepcopy(self.rows); rows[1]['iterations'] = value
            with self.assertRaises(ValueError): validate(rows, self.manifest, 3, 'timing')

    def allocation_rows(self):
        sample = {'event': 'allocation', 'case': 'example', 'variant': 'c', 'phase': 'batch',
                  'operations': 64, 'checksum': 4, 'malloc_calls': 0, 'calloc_calls': 0,
                  'realloc_calls': 0, 'free_calls': 0, 'aligned_calls': 0, 'failed_calls': 0,
                  'requested_bytes': 0, 'overflow_requests': 0, 'live_before': None,
                  'live_after': None, 'peak_live': None}
        return [{'event': 'case', 'case': 'example', 'variants': ['c', 'zig']}, sample,
                dict(sample, variant='zig'), {'event': 'meter_selftest', 'passed': True},
                {'event': 'complete', 'cases': 1, 'records': 2}]

    def test_positive_allocation_fixture(self):
        self.assertEqual(len(validate(self.allocation_rows(), self.manifest, 3, 'allocation', True)), 2)

    def test_missing_allocator_positive_control(self):
        rows = self.allocation_rows(); rows.pop(3)
        with self.assertRaises(ValueError): validate(rows, self.manifest, 3, 'allocation', True)

    def test_native_allocator_regression(self):
        rows = self.allocation_rows(); rows[1]['malloc_calls'] = 1
        with self.assertRaises(ValueError): validate(rows, self.manifest, 3, 'allocation', True)

    def test_shutdown_leak(self):
        rows = self.allocation_rows()
        rows.append(dict(rows[1], phase='close', live_before=64, live_after=8, peak_live=64))
        with self.assertRaises(ValueError): validate(rows, self.manifest, 3, 'allocation')

    def test_missing_lifecycle_rejected(self):
        with self.assertRaises(ValueError):
            validate(self.allocation_rows(), self.manifest, 3, 'allocation', lifecycle='sqlite')

    def test_successful_workload_may_not_hide_allocation_failure(self):
        rows = self.allocation_rows(); rows[1].update(malloc_calls=1, failed_calls=1)
        with self.assertRaises(ValueError): validate(rows, self.manifest, 3, 'allocation')

    def test_impossible_memory_peak(self):
        rows = self.allocation_rows(); rows[1].update(live_before=10, live_after=20, peak_live=15)
        with self.assertRaises(ValueError): validate(rows, self.manifest, 3, 'allocation')


if __name__ == '__main__':
    unittest.main()
