#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host regression checks of cohort selection and LAB flag propagation, not a kernel build."""
import ast,hashlib,json,os,subprocess,tempfile,unittest
from pathlib import Path
M=Path(__file__).resolve().parents[1]

class Profiles(unittest.TestCase):
    def wrapper(self,profile,fragment=True):
        with tempfile.TemporaryDirectory(prefix='pong build profile ')as tmp:
            w=Path(tmp);(w/'common/arch/arm64/configs').mkdir(parents=True)
            (w/'common/Makefile').touch()
            if fragment:(w/'common/arch/arm64/configs/pong_lab.fragment').touch()
            (w/'tools').mkdir();d=w/'devices/pong618';(d/'tools').mkdir(parents=True)
            (d/'BUILD.bazel').touch();ref=w/'chosen cohort.json';ref.write_text(json.dumps({'kernel_profile':profile}))
            log=w/'commands.jsonl'
            stub='#!/usr/bin/env python3\nimport json,os,sys\nwith open(os.environ["CALL_LOG"],"a")as f:f.write(json.dumps(sys.argv)+"\\n")\n'
            for p in [w/'tools/bazel',d/'tools/verify_build.py',d/'tools/prepare-modules.py']:
                p.write_text(stub);p.chmod(0o700)
            q=subprocess.run(['bash',str(M/'tools/build-pong.sh')],cwd=w,
                env=dict(os.environ,CALL_LOG=str(log),PONG_REFERENCE=str(ref),PONG_OUT=str(w/'output files')),
                capture_output=True,text=True,timeout=15)
            calls=[json.loads(s)for s in log.read_text().splitlines()]if log.exists()else[]
            return q,calls,str(ref)

    def test_lab_profile_reaches_both_kernel_and_module_builds(self):
        q,calls,ref=self.wrapper('lab');self.assertEqual(q.returncode,0,q.stderr)
        bazel=[c for c in calls if c[0].endswith('tools/bazel')]
        self.assertEqual(len(bazel),2)
        for c in bazel:self.assertIn('--defconfig_fragment=//common:pong_lab_defconfig',c)
        checks=[c for c in calls if c[0].endswith(('verify_build.py','prepare-modules.py'))]
        self.assertEqual(len(checks),2)
        for c in checks:self.assertEqual(c[c.index('--reference')+1],ref)

    def test_legacy_profile_never_silently_adds_a_fragment(self):
        q,calls,_=self.wrapper('legacy');self.assertEqual(q.returncode,0,q.stderr)
        self.assertFalse(any('--defconfig_fragment='in arg for c in calls for arg in c))

    def test_unknown_profile_refused_before_build(self):
        q,calls,_=self.wrapper('unexpected');self.assertNotEqual(q.returncode,0);self.assertEqual(calls,[])

    def test_lab_requires_the_actual_fragment_file(self):
        q,calls,_=self.wrapper('lab',False);self.assertNotEqual(q.returncode,0);self.assertEqual(calls,[])

    def test_declared_cohort_matches_all_output_targets_and_first_stage(self):
        r=json.loads((M/'docs/build-reference-6.18.60-CLG277.json').read_text())
        tree=ast.parse((M/'BUILD.bazel').read_text())
        call=next(n.value for n in tree.body if isinstance(n,ast.Expr)and isinstance(n.value,ast.Call)and getattr(n.value.func,'id','')=='pong_modules')
        targets=ast.literal_eval(next(k.value for k in call.keywords if k.arg=='modules'))
        outputs={name:label for label,names in targets.items()for name in names.split()}
        self.assertEqual(outputs,r['vendor']);self.assertEqual(len(outputs),193)
        self.assertEqual(len(r['gki']),84);self.assertEqual(r['expected_counts']['total'],277)
        p=M/r['first_stage_list'];names=p.read_text().split()
        self.assertEqual(len(names),100);self.assertEqual(len(set(names)),100)
        self.assertLessEqual(set(names),set(r['gki'])|set(r['vendor']))
        self.assertEqual(hashlib.sha256(p.read_bytes()).hexdigest(),r['first_stage_list_sha256'])

if __name__=='__main__':unittest.main(verbosity=2)
