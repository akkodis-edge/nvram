#!/usr/bin/python3

import unittest
import tempfile
import os
import subprocess
from subprocess import CalledProcessError

def nvram(env, arglist, sys=False):
    args = ['./nvram']
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
                'NVRAM_SYSTEM_A': f'{self.dir}/system_a',
                'NVRAM_SYSTEM_B': f'{self.dir}/system_b',
                'NVRAM_USER_A': f'{self.dir}/user_a',
                'NVRAM_USER_B': f'{self.dir}/user_b',
            }
        self.sys = False
    
    def tearDown(self):
        self.assertFalse(os.path.isfile(self.env['NVRAM_SYSTEM_A']))
        self.assertFalse(os.path.isfile(self.env['NVRAM_SYSTEM_B']))
        self.tmpdir.cleanup()
        
    def nvram_set(self, key, val):
        nvram(self.env, ['set', key, val], sys=self.sys)
        
    def nvram_get(self, key):
        return nvram(self.env, ['get', key], sys=self.sys).rstrip()
    
    def nvram_list(self):
        stdout = nvram(self.env, ['list'], sys=self.sys)
        return dict(pair.split("=") for pair in stdout.split())
    
    def nvram_delete(self, key):
        nvram(self.env, ['delete', key], sys=self.sys)

class test_user_set_get(test_user_base):
    def test_set_get(self):
        key = 'key1'
        val = 'val1'
        self.nvram_set(key, val)
        self.assertEqual(val, self.nvram_get(key))
     
    def test_set_get_multiple(self):
        attributes = {}
        for i in range(10):
            key = f'key{i}'
            val = f'val{i}'
            attributes[key] = val
            self.nvram_set(key, val)
        
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
        self.nvram_set(key, val1)
        self.nvram_set(key, val2)
        self.assertEqual(val2, self.nvram_get(key))
        
    def test_with_prefix(self):
        key = 'SYS_key1'
        val = 'val1'
        with self.assertRaises(CalledProcessError):
            self.nvram_set(key, val)

class test_user_list(test_user_base):
    def test_list(self):
        attributes = {}
        for i in range(10):
            key = f'key{i}'
            val = f'val{i}'
            attributes[key] = val
            self.nvram_set(key, val)
        d = self.nvram_list()
        self.assertEqual(d, attributes)
        
    def test_empty(self):
        d = self.nvram_list()
        self.assertEqual(0, len(d))
        
class test_user_delete(test_user_base):
    def test_delete(self):
        key = 'key1'
        val = 'val1'
        self.nvram_set(key, val)
        self.nvram_delete(key)
        with self.assertRaises(CalledProcessError):
            self.nvram_get(key)
    
    def test_empty(self):
        key = 'key1'
        self.nvram_delete(key)
  
'''

            SYSTEM mode (--sys)

'''
class test_system_base(test_user_base):
    def setUp(self):
        super().setUp()
        self.sys = True
        self.env['NVRAM_SYSTEM_UNLOCK'] = '16440'
    
    def tearDown(self):
        self.assertFalse(os.path.isfile(self.env['NVRAM_USER_A']))
        self.assertFalse(os.path.isfile(self.env['NVRAM_USER_B']))
        self.tmpdir.cleanup()

class test_system_set_get(test_system_base):
    def test_set_get(self):
        key = 'SYS_key1'
        val = 'val1'
        self.nvram_set(key, val)
        self.assertEqual(val, self.nvram_get(key))
     
    def test_set_get_multiple(self):
        attributes = {}
        for i in range(10):
            key = f'SYS_key{i}'
            val = f'val{i}'
            attributes[key] = val
            self.nvram_set(key, val)
        
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
        self.nvram_set(key, val1)
        self.nvram_set(key, val2)
        self.assertEqual(val2, self.nvram_get(key))
        
    def test_without_prefix(self):
        key = 'key1'
        val = 'val1'
        with self.assertRaises(CalledProcessError):
            self.nvram_set(key, val)
            
    def test_without_unlock(self):
        key = 'SYS_key1'
        val = 'val1'
        self.env.pop('NVRAM_SYSTEM_UNLOCK')
        with self.assertRaises(CalledProcessError):
            self.nvram_set(key, val)
        
class test_system_list(test_system_base):
    def test_list(self):
        attributes = {}
        for i in range(10):
            key = f'SYS_key{i}'
            val = f'val{i}'
            attributes[key] = val
            self.nvram_set(key, val)
        d = self.nvram_list()
        self.assertEqual(d, attributes)
        
    def test_empty(self):
        d = self.nvram_list()
        self.assertEqual(0, len(d))
        
class test_system_delete(test_system_base):
    def test_delete(self):
        key = 'SYS_key1'
        val = 'val1'
        self.nvram_set(key, val)
        self.nvram_delete(key)
        with self.assertRaises(CalledProcessError):
            self.nvram_get(key)
            
    def test_empty(self):
        key = 'SYS_key1'
        self.nvram_delete(key)
        
    def test_without_unlock(self):
        key = 'SYS_key1'
        self.env.pop('NVRAM_SYSTEM_UNLOCK')
        with self.assertRaises(CalledProcessError):
            self.nvram_delete(key)
            
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
        self.assertTrue(os.path.isfile(self.env['NVRAM_SYSTEM_A']))
        self.assertTrue(os.path.isfile(self.env['NVRAM_SYSTEM_B']))
        self.assertTrue(os.path.isfile(self.env['NVRAM_USER_A']))
        self.assertTrue(os.path.isfile(self.env['NVRAM_USER_B']))
        super().tearDown()
        
    def test_list(self):
        self.sys = True
        attributes = {}
        for i in range(10):
            key = f'SYS_key{i}'
            val = f'val{i}'
            attributes[key] = val
            self.nvram_set(key, val)
            
        self.sys = False
        for i in range(10):
            key = f'key{i}'
            val = f'val{i}'
            attributes[key] = val
            self.nvram_set(key, val)
            
        d = self.nvram_list()
        self.assertEqual(d, attributes)

class test_mixed_delete(test_mixed_base):
    def tearDown(self):
        self.assertTrue(os.path.isfile(self.env['NVRAM_SYSTEM_A']))
        self.assertTrue(os.path.isfile(self.env['NVRAM_USER_A']))
        super().tearDown()
        
    def test_delete_user(self):
        sys_key1 = 'SYS_key1'
        sys_val1 = 'SYS_val1'
        key1 = 'key1'
        val1 = 'val1'
        self.sys=True
        self.nvram_set(sys_key1, sys_val1)
        self.sys=False
        self.nvram_set(key1, val1)
        self.nvram_delete(key1)
        
        d = self.nvram_list()
        self.assertEqual(d, {sys_key1: sys_val1})

    def test_system_delete(self):
        sys_key1 = 'SYS_key1'
        sys_val1 = 'SYS_val1'
        key1 = 'key1'
        val1 = 'val1'
        self.sys=False
        self.nvram_set(key1, val1)
        self.sys=True
        self.nvram_set(sys_key1, sys_val1)
        self.nvram_delete(sys_key1)
        
        self.sys=False
        d = self.nvram_list()
        self.assertEqual(d, {key1: val1})
        
class test_single_section(test_user_base):
    def setUp(self):
        self.tmpdir = tempfile.TemporaryDirectory()
        self.dir = self.tmpdir.name
        self.env = {
                'NVRAM_SYSTEM_A': f'{self.dir}/system_a',
                'NVRAM_SYSTEM_B': f'{self.dir}/system_b',
                'NVRAM_USER_A': '',
                'NVRAM_USER_B': '',
            }
        self.sys = False
        
    def tearDown(self):
        self.tmpdir.cleanup()
        
    def test_single_a(self):
        self.env['NVRAM_USER_A'] = f'{self.dir}/user_a'
        attributes = {}
        for i in range(10):
            key = f'key{i}'
            val = f'val{i}'
            attributes[key] = val
            self.nvram_set(key, val)
        
        for key, value in attributes.items():
            read = self.nvram_get(key)
            self.assertEqual(read, value)
            
        self.assertTrue(os.path.isfile(f'{self.dir}/user_a'))
        self.assertFalse(os.path.isfile(f'{self.dir}/user_b'))
            
    def test_single_b(self):
        self.env['NVRAM_USER_B'] = f'{self.dir}/user_b'
        attributes = {}
        for i in range(10):
            key = f'key{i}'
            val = f'val{i}'
            attributes[key] = val
            self.nvram_set(key, val)
        
        for key, value in attributes.items():
            read = self.nvram_get(key)
            self.assertEqual(read, value)
            
        self.assertTrue(os.path.isfile(f'{self.dir}/user_b'))
        self.assertFalse(os.path.isfile(f'{self.dir}/user_a'))

if __name__ == '__main__':
    unittest.main()