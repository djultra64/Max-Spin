"""Actual native helper command arguments; no title or Windows UI. djultra64."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CommandContract(unittest.TestCase):
    def test_windows_arguments_preserve_spaces_quotes_unicode_and_backslash(self):
        source = r'''
#include <stddef.h>
#include <stdio.h>
#include <wchar.h>
extern int topspin_windows_character_command(wchar_t*,size_t,unsigned,const wchar_t*const*) __attribute__((weak));
int main(void){
 const wchar_t *args[]={L"C:/Python 3/python.exe",L"",L"a b",L"a\"b",L"x\\",L"ni\u00f1o"};
 const wchar_t *want=L"\"C:/Python 3/python.exe\" \"\" \"a b\" \"a\\\"b\" \"x\\\\\" \"ni\u00f1o\"";
 wchar_t out[256];
 if(!topspin_windows_character_command||!topspin_windows_character_command(out,256,6,args)||wcscmp(out,want)){fputs("argument roundtrip missing\n",stderr);return 1;}
 if(topspin_windows_character_command(out,3,6,args)||out[0]){fputs("bounded failure missing\n",stderr);return 1;}
 if(topspin_windows_character_command(out,256,0,args)||out[0])return 1;
 return 0;
}'''
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            fixture = folder / 'fixture.c'
            fixture.write_text(source)
            command = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(fixture)]
            production = ROOT / 'windows/native/src/character_command.c'
            if production.exists():
                command.append(str(production))
            binary = folder / 'fixture'
            subprocess.run(command + ['-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
