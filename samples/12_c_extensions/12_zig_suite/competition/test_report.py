import unittest
from run import head_to_head

class CompetitionReportTests(unittest.TestCase):
    def setUp(self):
        self.manifest = {'rival/test': ['c_scalar','zig_previous','c_tuned','zig_tuned']}
        self.rows = [dict(event='timing',process=p,case='rival/test',trial=t,variant=v,
                          nanoseconds=n,iterations=1)
                     for p in range(2) for t in range(3)
                     for v,n in zip(self.manifest['rival/test'],(400,200,100,80))]

    def test_all_three_comparisons(self):
        records, _ = head_to_head(self.rows,self.manifest)
        self.assertEqual([r['paired_ratio'] for r in records],[4,2.5,1.25])
        self.assertEqual(len(records),3)

    def test_retain_losing_variant(self):
        for row in self.rows:
            if row['variant']=='zig_tuned': row['nanoseconds']=800
        records, table = head_to_head(self.rows,self.manifest)
        self.assertEqual(records[-1]['paired_ratio'],.125)
        self.assertIn('0.125x',table)

    def test_missing_reference_is_not_reportable(self):
        rows = [r for r in self.rows if r['variant']!='zig_previous']
        with self.assertRaises(KeyError): head_to_head(rows,self.manifest)

    def test_process_pairs_not_ratio_of_pooled_medians(self):
        for row in self.rows:
            if row['process']==1:
                if row['variant']=='c_tuned': row['nanoseconds']=1000
                if row['variant']=='zig_tuned': row['nanoseconds']=100
        records, _ = head_to_head(self.rows,self.manifest)
        self.assertEqual(records[-1]['process_ratios'],[1.25,10])
        self.assertEqual(records[-1]['paired_ratio'],5.625)

if __name__=='__main__': unittest.main()
