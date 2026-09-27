#!/usr/bin/env python3
"""Real CTest fixtures preserve every selected case and strict failure outcomes."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


TOOL = Path(__file__).resolve().parents[1] / 'tools/run-native-tests.py'
SPEC = importlib.util.spec_from_file_location('native_tests_runner', TOOL)
helper = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(helper)
MARKER = ('INCOMPLETE GUI_SMOKE_BUDGET: phase=17 elapsed=600.001000 budget=600.000000 '
          'tx_id=13 fraction=0.100000 media_seconds=10.000000 samples=1234 '
          'tail=0 progress_age=0.001000 result=incomplete')


class NativeSelectionTests(unittest.TestCase):
    def test_duplicate_inventory_and_empty_selection_are_fatal(self):
        test = {'name':'gui_workflow','properties':[{'name':'LABELS','value':['native_gui']}]}
        with self.assertRaisesRegex(ValueError,'duplicate'):
            helper.select_tests([test,test],'native_gui')
        with self.assertRaisesRegex(ValueError,'no tests'):
            helper.select_tests([test],'missing')

    def test_direct_properties_are_preserved_or_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            test = {'name':'gui_workflow','command':['executable','--smoke-test'],
                    'properties':[{'name':'TIMEOUT','value':630},
                                  {'name':'WORKING_DIRECTORY','value':directory},
                                  {'name':'ENVIRONMENT','value':['EXACT_VALUE=a=b']}]}
            command,cwd,environment,timeout = helper.direct_context(test,Path(directory))
            self.assertEqual(command,test['command'])
            self.assertEqual(cwd,Path(directory).resolve())
            self.assertEqual(environment['EXACT_VALUE'],'a=b')
            self.assertEqual(timeout,630)
            for name in ('ENVIRONMENT_MODIFICATION','FIXTURES_REQUIRED','SKIP_RETURN_CODE',
                         'WILL_FAIL','FAIL_REGULAR_EXPRESSION','DISABLED','RESOURCE_LOCK'):
                with self.subTest(name=name), self.assertRaisesRegex(ValueError,'Cannot directly preserve'):
                    helper.direct_context(dict(test,properties=test['properties']+[{'name':name,'value':True}]),Path(directory))


@unittest.skipUnless(shutil.which('cmake') and shutil.which('ctest'),'CMake/CTest fixtures require installed tools')
class NativeRunnerTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='native runner ')
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.work = self.root/'working directory';self.work.mkdir()
        self.build = self.root/'build'
        self.reports = self.root/'reports'
        self.stamp = self.root/'executed.txt'
        self.fixture = self.root/'fixture.py'
        self.fixture.write_text(
            'import os,sys,time\nfrom pathlib import Path\n'
            'mode=sys.argv[1]\n'
            f'assert Path.cwd()==Path({str(self.work)!r})\n'
            'assert os.environ.get("PROBE_VALUE")=="hello world"\n'
            f'with open({str(self.stamp)!r},"a") as f:f.write(mode+"\\n")\n'
            f'if mode in ("budget","budget_sleep"):print({MARKER!r},flush=True)\n'
            'if mode=="budget":sys.exit(75)\n'
            'if mode in ("sleep","budget_sleep"):time.sleep(30)\n'
            'if mode=="failure":print("physical assertion failed");sys.exit(1)\n'
            'if mode=="sanitizer":print("ERROR: AddressSanitizer: heap-use-after-free")\n'
            'if mode=="slow":time.sleep(.05)\n'
            'if mode=="timing":print("TEST_WORKLOAD_BUDGET: fixture completed with additional workload time")\n')

    def configure(self, smoke='budget', adapter='adapter', remaining='platform', timeout=2, extra=''):
        quote = lambda value: json.dumps(str(value).replace('\\','/'))
        lines = ['cmake_minimum_required(VERSION 3.21)','project(Fixture NONE)','enable_testing()']
        for name,mode in [('gui_workflow',smoke),('gui_adapter_conformance',adapter),('gui_platform_conformance',remaining)]:
            lines.append(f'add_test(NAME {name} COMMAND {quote(sys.executable)} {quote(self.fixture)} {mode})')
            lines.append(f'set_tests_properties({name} PROPERTIES LABELS "gui;native_gui" TIMEOUT {timeout} '
                         f'WORKING_DIRECTORY {quote(self.work)} ENVIRONMENT "PROBE_VALUE=hello world")')
        lines += ['add_test(NAME calibration COMMAND '+quote(sys.executable)+' -c "raise SystemExit(9)")',
                  'set_tests_properties(calibration PROPERTIES LABELS "gui;calibration")',extra]
        (self.root/'CMakeLists.txt').write_text('\n'.join(lines)+'\n')
        subprocess.run(['cmake','-S',str(self.root),'-B',str(self.build)],capture_output=True,check=True)

    def invoke(self,*extra):
        env = dict(os.environ,DATAPUMP_SMOKE_TARGET='linux-aarch64-rev',DATAPUMP_SMOKE_SCOPE='source/native',
                   DATAPUMP_SMOKE_SOURCE_SHA='a'*40,DATAPUMP_SMOKE_INVENTORY_SHA256='b'*64,
                   GITHUB_RUN_ID='123',GITHUB_RUN_ATTEMPT='1')
        return subprocess.run([sys.executable,str(TOOL),'--build-dir',str(self.build),
                               '--ctest',shutil.which('ctest'),'--config','Release',
                               '--report-dir',str(self.reports),*extra],env=env,
                              capture_output=True,text=True,timeout=15)

    def report(self):
        return json.loads((self.reports/'native-tests.json').read_text())

    def test_incomplete_smoke_retains_all_other_tests_and_exact_identity(self):
        self.configure()
        result=self.invoke('--allow-budget-exhaustion')
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        report=self.report()
        self.assertEqual(report['status'],'passed_with_warnings')
        self.assertEqual({x['name']:x['status'] for x in report['tests']},
                         {'gui_workflow':'incomplete','gui_adapter_conformance':'passed','gui_platform_conformance':'passed'})
        self.assertEqual(sorted(self.stamp.read_text().splitlines()),['adapter','budget','platform'])
        warning=json.loads((self.reports/'gui_workflow.json').read_text())['warning']
        self.assertEqual(warning['diagnostic'],MARKER)
        self.assertEqual(warning['source_sha'],'a'*40)
        self.assertTrue((self.reports/'native-remaining.xml').exists())

    def test_smoke_is_strict_without_policy(self):
        self.configure()
        result=self.invoke()
        self.assertNotEqual(result.returncode,0)
        self.assertEqual(self.report()['status'],'failed')

    def test_full_pass_preserves_label_exclusion_and_timing_warning(self):
        self.configure(smoke='smoke',remaining='timing')
        result=self.invoke('--label','gui','--exclude-label','calibration')
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertEqual(self.report()['status'],'passed')
        self.assertIn('::warning title=CI test workload::',result.stdout)
        self.assertNotIn('calibration',self.report()['selected'])

    def test_complementary_assertion_fails_fast_and_does_not_run_smoke(self):
        self.configure(remaining='failure')
        result=self.invoke('--allow-budget-exhaustion')
        self.assertNotEqual(result.returncode,0)
        self.assertEqual(self.report()['status'],'failed')
        self.assertNotIn('budget',self.stamp.read_text().splitlines())

    def test_registered_process_deadline_remains_fatal(self):
        self.configure(smoke='budget_sleep',timeout=.2)
        result=self.invoke('--allow-budget-exhaustion')
        self.assertNotEqual(result.returncode,0)
        self.assertEqual(self.report()['status'],'failed')
        self.assertNotIn('Incomplete GUI smoke coverage',result.stdout)

    def test_direct_adapter_all_assertions_and_success_only_warning(self):
        self.configure(smoke='smoke',adapter='slow')
        result=self.invoke('--adapter-timeout','2','--adapter-warn-after','.01')
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('::warning title=Rev adapter workload::',result.stdout)
        self.assertEqual(sorted(self.stamp.read_text().splitlines()),['platform','slow','smoke'])

    def test_direct_adapter_failure_timeout_and_sanitizer_remain_fatal(self):
        for mode in ('failure','sleep','sanitizer'):
            with self.subTest(mode=mode):
                self.configure(smoke='smoke',adapter=mode)
                result=self.invoke('--adapter-timeout','.2','--adapter-warn-after','.01')
                self.assertNotEqual(result.returncode,0)
                self.assertEqual(self.report()['status'],'failed')
                self.assertNotIn('Rev adapter workload',result.stdout)
                shutil.rmtree(self.reports)

    def test_unsupported_direct_properties_fail_before_execution(self):
        self.configure(extra='set_tests_properties(gui_workflow PROPERTIES SKIP_RETURN_CODE 75)')
        result=self.invoke('--allow-budget-exhaustion')
        self.assertNotEqual(result.returncode,0)
        self.assertFalse(self.stamp.exists())
        self.assertIn('Cannot directly preserve',self.report()['error'])

    def test_report_no_clobber(self):
        self.configure(smoke='smoke')
        self.assertEqual(self.invoke().returncode,0)
        original=(self.reports/'native-tests.json').read_bytes()
        result=self.invoke()
        self.assertNotEqual(result.returncode,0)
        self.assertEqual((self.reports/'native-tests.json').read_bytes(),original)


if __name__=='__main__':
    unittest.main()
