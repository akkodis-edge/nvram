#!/usr/bin/python3

import unittest
import tempfile
import os
import subprocess
from subprocess import CalledProcessError

def nvram(env, arglist, sys=False):
    args = ['./build/nvram']
    if sys:
        args.append('--sys')
    args.extend(arglist)
    r = subprocess.run(args, capture_output=True, text=True, env=env, check=True)
    return r.stdout

'''

            USER mode

'''   
class test_user_base(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.TemporaryDirectory()
        self.dir = self.tmpdir.name
        self.env = {
                'NVRAM_INTERFACE': 'file',
                'NVRAM_FILE_SYSTEM_A': f'{self.dir}/system_a',
                'NVRAM_FILE_SYSTEM_B': f'{self.dir}/system_b',
                'NVRAM_FILE_USER_A': f'{self.dir}/user_a',
                'NVRAM_FILE_USER_B': f'{self.dir}/user_b',
            }
        self.sys = False
    
    def tearDown(self):
        self.assertFalse(os.path.isfile(self.env['NVRAM_FILE_SYSTEM_A']))
        self.assertFalse(os.path.isfile(self.env['NVRAM_FILE_SYSTEM_B']))
        self.tmpdir.cleanup()
        
    def nvram_set(self, pairs):
        args = []
        for key, val in pairs:
            args.extend(['--set', key, val])
        nvram(self.env, args, sys=self.sys)
        
    def nvram_get(self, key):
        return nvram(self.env, ['--get', key], sys=self.sys).rstrip()
    
    def nvram_list(self):
        stdout = nvram(self.env, ['--list'], sys=self.sys)
        return dict(pair.split("=") for pair in stdout.split())
    
    def nvram_delete(self, keys):
        args = []
        for key in keys:
            args.extend(['--del', key])
        nvram(self.env, args, sys=self.sys)

class test_user_set_get(test_user_base):
    def test_set_get(self):
        key = 'key1'
        val = 'val1'
        self.nvram_set([(key, val)])
        self.assertEqual(val, self.nvram_get(key))
     
    def test_set_get_multiple(self):
        attributes = {}
        for i in range(10):
            key = f'key{i}'
            val = f'val{i}'
            attributes[key] = val
            self.nvram_set([(key, val)])
        
        for key, value in attributes.items():
            read = self.nvram_get(key)
            self.assertEqual(read, value)
        
    def test_empty(self):
        key = 'key1'
        with self.assertRaises(CalledProcessError):
            self.nvram_get(key)
            
    def test_overwrite(self):
        key = 'key1'
        val1 = 'val1'
        val2 = 'val2'
        self.nvram_set([(key, val1)])
        self.nvram_set([(key, val2)])
        self.assertEqual(val2, self.nvram_get(key))
        
    def test_with_prefix(self):
        key = 'SYS_key1'
        val = 'val1'
        with self.assertRaises(CalledProcessError):
            self.nvram_set([(key, val)])

class test_user_list(test_user_base):
    def test_list(self):
        attributes = {}
        for i in range(10):
            key = f'key{i}'
            val = f'val{i}'
            attributes[key] = val
            self.nvram_set([(key, val)])
        d = self.nvram_list()
        self.assertEqual(d, attributes)
        
    def test_empty(self):
        d = self.nvram_list()
        self.assertEqual(0, len(d))
        
class test_user_delete(test_user_base):
    def test_delete(self):
        key = 'key1'
        val = 'val1'
        self.nvram_set([(key, val)])
        self.nvram_delete([key])
        with self.assertRaises(CalledProcessError):
            self.nvram_get(key)
    
    def test_empty(self):
        key = 'key1'
        self.nvram_delete([key])
        
class test_user_multi_set_delete(test_user_base):
    def test_delete(self):
        key1 = 'key1'
        val1 = 'val1'
        key2 = 'key2'
        val2 = 'val2'
        self.nvram_set([(key1, val1), (key2, val2)])
        self.assertEqual(val1, self.nvram_get(key1))
        self.assertEqual(val2, self.nvram_get(key2))
        self.nvram_delete([key1, key2])
        with self.assertRaises(CalledProcessError):
            self.nvram_get(key1)
        with self.assertRaises(CalledProcessError):
            self.nvram_get(key2)

'''

            SYSTEM mode (--sys)

'''
class test_system_base(test_user_base):
    def setUp(self):
        super().setUp()
        self.sys = True
        self.env['NVRAM_SYSTEM_UNLOCK'] = '16440'
    
    def tearDown(self):
        self.assertFalse(os.path.isfile(self.env['NVRAM_FILE_USER_A']))
        self.assertFalse(os.path.isfile(self.env['NVRAM_FILE_USER_B']))
        self.tmpdir.cleanup()

class test_system_set_get(test_system_base):
    def test_set_get(self):
        key = 'SYS_key1'
        val = 'val1'
        self.nvram_set([(key, val)])
        self.assertEqual(val, self.nvram_get(key))
     
    def test_set_get_multiple(self):
        attributes = {}
        for i in range(10):
            key = f'SYS_key{i}'
            val = f'val{i}'
            attributes[key] = val
            self.nvram_set([(key, val)])
        
        for key, value in attributes.items():
            read = self.nvram_get(key)
            self.assertEqual(read, value)
        
    def test_empty(self):
        key = 'SYS_key1'
        with self.assertRaises(CalledProcessError):
            self.nvram_get(key)
            
    def test_overwrite(self):
        key = 'SYS_key1'
        val1 = 'val1'
        val2 = 'val2'
        self.nvram_set([(key, val1)])
        self.nvram_set([(key, val2)])
        self.assertEqual(val2, self.nvram_get(key))
        
    def test_without_prefix(self):
        key = 'key1'
        val = 'val1'
        with self.assertRaises(CalledProcessError):
            self.nvram_set([(key, val)])
            
    def test_without_unlock(self):
        key = 'SYS_key1'
        val = 'val1'
        self.env.pop('NVRAM_SYSTEM_UNLOCK')
        with self.assertRaises(CalledProcessError):
            self.nvram_set([(key, val)])
        
class test_system_list(test_system_base):
    def test_list(self):
        attributes = {}
        for i in range(10):
            key = f'SYS_key{i}'
            val = f'val{i}'
            attributes[key] = val
            self.nvram_set([(key, val)])
        d = self.nvram_list()
        self.assertEqual(d, attributes)
        
    def test_empty(self):
        d = self.nvram_list()
        self.assertEqual(0, len(d))
        
class test_system_delete(test_system_base):
    def test_delete(self):
        key = 'SYS_key1'
        val = 'val1'
        self.nvram_set([(key, val)])
        self.nvram_delete([key])
        with self.assertRaises(CalledProcessError):
            self.nvram_get(key)
            
    def test_empty(self):
        key = 'SYS_key1'
        self.nvram_delete([key])
        
    def test_without_unlock(self):
        key = 'SYS_key1'
        self.env.pop('NVRAM_SYSTEM_UNLOCK')
        with self.assertRaises(CalledProcessError):
            self.nvram_delete([key])

class test_system_multi_set_delete(test_system_base):
    def test_delete(self):
        key1 = 'SYS_key1'
        val1 = 'val1'
        key2 = 'SYS_key2'
        val2 = 'val2'
        self.nvram_set([(key1, val1), (key2, val2)])
        self.assertEqual(val1, self.nvram_get(key1))
        self.assertEqual(val2, self.nvram_get(key2))
        self.nvram_delete([key1, key2])
        with self.assertRaises(CalledProcessError):
            self.nvram_get(key1)
        with self.assertRaises(CalledProcessError):
            self.nvram_get(key2)    
'''

            Mixed mode

'''
class test_mixed_base(test_user_base):
    def setUp(self):
        super().setUp()
        self.env['NVRAM_SYSTEM_UNLOCK'] = '16440'
    
    def tearDown(self):
        self.tmpdir.cleanup()

class test_mixed_list(test_mixed_base):
    def tearDown(self):
        self.assertTrue(os.path.isfile(self.env['NVRAM_FILE_SYSTEM_A']))
        self.assertTrue(os.path.isfile(self.env['NVRAM_FILE_SYSTEM_B']))
        self.assertTrue(os.path.isfile(self.env['NVRAM_FILE_USER_A']))
        self.assertTrue(os.path.isfile(self.env['NVRAM_FILE_USER_B']))
        super().tearDown()
        
    def test_list(self):
        self.sys = True
        attributes = {}
        for i in range(10):
            key = f'SYS_key{i}'
            val = f'val{i}'
            attributes[key] = val
            self.nvram_set([(key, val)])
            
        self.sys = False
        for i in range(10):
            key = f'key{i}'
            val = f'val{i}'
            attributes[key] = val
            self.nvram_set([(key, val)])
            
        d = self.nvram_list()
        self.assertEqual(d, attributes)

class test_mixed_delete(test_mixed_base):
    def tearDown(self):
        self.assertTrue(os.path.isfile(self.env['NVRAM_FILE_SYSTEM_A']))
        self.assertTrue(os.path.isfile(self.env['NVRAM_FILE_USER_A']))
        super().tearDown()
        
    def test_delete_user(self):
        sys_key1 = 'SYS_key1'
        sys_val1 = 'SYS_val1'
        key1 = 'key1'
        val1 = 'val1'
        self.sys=True
        self.nvram_set([(sys_key1, sys_val1)])
        self.sys=False
        self.nvram_set([(key1, val1)])
        self.nvram_delete([key1])
        
        d = self.nvram_list()
        self.assertEqual(d, {sys_key1: sys_val1})

    def test_system_delete(self):
        sys_key1 = 'SYS_key1'
        sys_val1 = 'SYS_val1'
        key1 = 'key1'
        val1 = 'val1'
        self.sys=False
        self.nvram_set([(key1, val1)])
        self.sys=True
        self.nvram_set([(sys_key1, sys_val1)])
        self.nvram_delete([sys_key1])
        
        self.sys=False
        d = self.nvram_list()
        self.assertEqual(d, {key1: val1})
        
class test_single_section(test_user_base):
    def setUp(self):
        self.tmpdir = tempfile.TemporaryDirectory()
        self.dir = self.tmpdir.name
        self.env = {
                'NVRAM_FILE_SYSTEM_A': f'{self.dir}/system_a',
                'NVRAM_FILE_SYSTEM_B': f'{self.dir}/system_b',
                'NVRAM_FILE_USER_A': '',
                'NVRAM_FILE_USER_B': '',
            }
        self.sys = False
        
    def tearDown(self):
        self.tmpdir.cleanup()
        
    def test_single_a(self):
        self.env['NVRAM_FILE_USER_A'] = f'{self.dir}/user_a'
        attributes = {}
        for i in range(10):
            key = f'key{i}'
            val = f'val{i}'
            attributes[key] = val
            self.nvram_set([(key, val)])
        
        for key, value in attributes.items():
            read = self.nvram_get(key)
            self.assertEqual(read, value)
            
        self.assertTrue(os.path.isfile(f'{self.dir}/user_a'))
        self.assertFalse(os.path.isfile(f'{self.dir}/user_b'))
            
    def test_single_b(self):
        self.env['NVRAM_FILE_USER_B'] = f'{self.dir}/user_b'
        attributes = {}
        for i in range(10):
            key = f'key{i}'
            val = f'val{i}'
            attributes[key] = val
            self.nvram_set([(key, val)])
        
        for key, value in attributes.items():
            read = self.nvram_get(key)
            self.assertEqual(read, value)
            
        self.assertTrue(os.path.isfile(f'{self.dir}/user_b'))
        self.assertFalse(os.path.isfile(f'{self.dir}/user_a'))

class test_legacy_api(test_user_base):
    def nvram_legacy_set(self, pairs):
        args = []
        for key, val in pairs:
            args.extend(['set', key, val])
        nvram(self.env, args, sys=self.sys)
        
    def nvram_legacy_get(self, key):
        return nvram(self.env, ['get', key], sys=self.sys).rstrip()
    
    def nvram_legacy_list(self):
        stdout = nvram(self.env, ['list'], sys=self.sys)
        return dict(pair.split("=") for pair in stdout.split())
    
    def nvram_legacy_delete(self, keys):
        args = []
        for key in keys:
            args.extend(['delete', key])
        nvram(self.env, args, sys=self.sys)
    
    def test_set_get(self):
        key = 'key1'
        val = 'val'
        self.nvram_legacy_set([(key, val)])
        self.assertEqual(val, self.nvram_legacy_get(key))

    def test_set_list(self):
        key1 = 'key1'
        val1 = 'val1'
        key2 = 'key2'
        val2 = 'val2'
        self.nvram_legacy_set([(key1, val1), (key2, val2)])
        d = self.nvram_legacy_list()
        self.assertEqual(d[key1], val1)
        self.assertEqual(d[key2], val2)
        
    def test_set_delete(self):
        key = 'key'
        val = 'val'
        self.nvram_legacy_set([(key, val)])
        self.assertEqual(val, self.nvram_legacy_get(key))
        self.nvram_legacy_delete([key])
        with self.assertRaises(CalledProcessError):
            self.nvram_legacy_get(key)
            
class test_legacy_format(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.TemporaryDirectory()
        self.dir = self.tmpdir.name
        self.env = {
                'NVRAM_FORMAT': 'legacy',
                'NVRAM_FILE_SYSTEM_A': f'{self.dir}/system_a',
                'NVRAM_FILE_SYSTEM_B': '',
                'NVRAM_FILE_USER_A': f'{self.dir}/user_a',
                'NVRAM_FILE_USER_B': '',
            }
        self.sys = False
    
    def tearDown(self):
        self.assertFalse(os.path.isfile(self.env['NVRAM_FILE_SYSTEM_A']))
        self.tmpdir.cleanup()
        
    def nvram_set(self, pairs):
        args = []
        for key, val in pairs:
            args.extend(['--set', key, val])
        nvram(self.env, args, sys=self.sys)
        
    def nvram_get(self, key):
        return nvram(self.env, ['--get', key], sys=self.sys).rstrip()
    
    def read_user_a(self):
        with open(self.env['NVRAM_FILE_USER_A'], 'r') as f:
            return f.read()

    def write_user_a(self, buf):
        with open(self.env['NVRAM_FILE_USER_A'], 'w') as f:
            f.write(buf)

    def test_write(self):
        key1 = 'key1'
        val1 = 'val1'
        self.nvram_set([(key1, val1)])
        expects = f'{key1}={val1}\n'
        self.assertEqual(expects, self.read_user_a())
        
    def test_append(self):
        key1 = 'key1'
        val1 = 'val1'
        key2 = 'key2'
        val2 = 'val2'
        self.write_user_a(f'{key1}={val1}\n')
        self.nvram_set([(key2, val2)])
        expects = f'{key1}={val1}\n{key2}={val2}\n'
        self.assertEqual(expects, self.read_user_a())
        
    def test_get(self):
        key1 = 'key1'
        val1 = 'val1'
        self.write_user_a(f'{key1}={val1}\n')
        self.assertEqual(val1, self.nvram_get(key1))
        
    def test_get_multi(self):
        key1 = 'key1'
        val1 = 'val1'
        key2 = 'key2'
        val2 = 'val2'
        self.write_user_a(f'{key1}={val1}\n{key2}={val2}\n')
        self.assertEqual(val1, self.nvram_get(key1))
        self.assertEqual(val2, self.nvram_get(key2))
        
    def test_no_terminating_newline(self):
        key1 = 'key1'
        val1 = 'val1'
        self.write_user_a(f'{key1}={val1}')
        self.assertEqual(val1, self.nvram_get(key1))
        
    def test_errors(self):
        self.write_user_a(f'key1=val1\nkey2')
        with self.assertRaises(CalledProcessError):
            self.nvram_get('key1')
        self.write_user_a(f'key1\n')
        with self.assertRaises(CalledProcessError):
            self.nvram_get('key1')
        self.write_user_a(f'key1')
        with self.assertRaises(CalledProcessError):
            self.nvram_get('key1')
        self.write_user_a(f'key1=')
        with self.assertRaises(CalledProcessError):
            self.nvram_get('key1')
        self.write_user_a(f'key1=\n')
        with self.assertRaises(CalledProcessError):
            self.nvram_get('key1')
            
class test_platform_format(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.TemporaryDirectory()
        self.dir = self.tmpdir.name
        self.env = {
                'NVRAM_FORMAT': 'platform',
                'NVRAM_INTERFACE': 'file',
                'NVRAM_FILE_USER_A': f'{self.dir}/user_a',
                'NVRAM_FILE_USER_B': '',
            }
        self.sys = False

    def tearDown(self):
        self.tmpdir.cleanup()
        
    def nvram_set(self, pairs):
        args = ['--user']
        for key, val in pairs:
            args.extend(['--set', key, val])
        nvram(self.env, args, sys=self.sys)

    def nvram_get(self, key):
        return nvram(self.env, ['--user', '--get', key], sys=self.sys).rstrip()

    def nvram_list(self):
        stdout = nvram(self.env, ['--user', '--list'], sys=self.sys)
        return dict(pair.split("=") for pair in stdout.split())
    
    def test_version_0(self):
        version_0_fields = {
            'name': 'test_name',
            'ddrc_blob_offset': '0x1',
            'ddrc_blob_size': '0x2',
            'ddrc_blob_type': '0x3',
            'ddrc_blob_crc32': '0x4',
            'ddrc_size': '0x5',
            'config1': '0x6',
            'config2': '0x7',
            'config3': '0x8',
            'config4': '0x9',
            }
        self.nvram_set([(key, val) for key, val in version_0_fields.items()])
        ret = self.nvram_list()
        self.assertEqual(version_0_fields, ret)
        
    def test_field_u32(self):
        key = 'config1'
        values_ok = {
            '0xffffffff': '0xffffffff',
            '0XFFFFFFFF': '0xffffffff',
            '4294967295': '0xffffffff',
            '0x0': '0x0',
            '0X0': '0x0',
            '0': '0x0',
            }
        for val, ret in values_ok.items():
            self.nvram_set([(key, val)])
            self.assertEqual(ret, self.nvram_get(key))
            
        values_err = [
            '0xffffffffff',
            '4294967296',
            ]
        for val in values_err:
            with self.assertRaises(CalledProcessError):
                self.nvram_set([(key, val)])
                
    def test_field_u64(self):
        key = 'ddrc_size'
        values_ok = {
            '0xffffffffffffffff': '0xffffffffffffffff',
            '0XFFFFFFFFFFFFFFFF': '0xffffffffffffffff',
            '18446744073709551615': '0xffffffffffffffff',
            '0x0': '0x0',
            '0X0': '0x0',
            '0': '0x0',
            }
        for val, ret in values_ok.items():
            self.nvram_set([(key, val)])
            self.assertEqual(ret, self.nvram_get(key))
            
        values_err = [
            '0xffffffffffffffffff',
            '18446744073709551616',
            ]
        for val in values_err:
            with self.assertRaises(CalledProcessError):
                self.nvram_set([(key, val)])
        
if __name__ == '__main__':
    unittest.main()
    