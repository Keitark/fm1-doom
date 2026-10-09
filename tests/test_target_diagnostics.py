import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class TargetDiagnosticTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "nt", "Windows guard-page contract")
    def test_fatal_formatting_and_status_snapshot_are_bounded(self):
        cmake = shutil.which("cmake")
        if not cmake:
            self.skipTest("CMake and a host compiler are required")
        target = (ROOT / "src/fm1_doom_target.c").read_text(encoding="utf-8")
        size = int(re.search(r"char fm1_doom_error_message\[(\d+)\];", target).group(1))
        self.assertEqual(size, 192)
        # Exercise the actual target statements without mocking the SDK getter.
        copy = re.search(r"    memcpy\(status->error_message,.*?(?=    status->now_ms)",
                         target, re.S).group(0)
        formatting = re.search(r"    vsnprintf\(fm1_doom_error_message,.*?;", target).group(0)
        source = r'''
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fm1_doom_usb.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int __wrap_vsnprintf(char *, size_t, const char *, va_list);
static unsigned char *source_end;
#define fm1_doom_error_message (*(char (*)[SIZE]) (source_end-SIZE))
static void fatal_text(const char *format, ...){
    va_list args;va_start(args,format);
#define vsnprintf __wrap_vsnprintf
FORMATTING
#undef vsnprintf
    va_end(args);
}
static void snapshot(struct fm1_doom_usb_status *status){
COPY
}
/* Keep the old copy observable even in a Release build. */
static __declspec(noinline) void old_snapshot(struct fm1_doom_usb_status *status){
    memcpy(status->error_message,fm1_doom_error_message,sizeof(status->error_message));
}
int main(int argc,char **argv){
    SYSTEM_INFO info;DWORD previous;unsigned char *storage;size_t i;
    struct {unsigned char before[32];struct fm1_doom_usb_status status;
            unsigned char after[32];} guarded;
    char long_text[513];
    (void)argv;SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    GetSystemInfo(&info);CHECK(info.dwPageSize>SIZE);
    storage=VirtualAlloc(NULL,info.dwPageSize*2,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(storage);
    CHECK(VirtualProtect(storage+info.dwPageSize,info.dwPageSize,PAGE_NOACCESS,&previous));
    source_end=storage+info.dwPageSize;
    memset(fm1_doom_error_message,'x',sizeof(fm1_doom_error_message));
    memset(&guarded,0xa5,sizeof(guarded));
    CHECK(sizeof(guarded.status.error_message)==256);
    if(argc>1){old_snapshot(&guarded.status);return guarded.status.error_message[255];}
    snapshot(&guarded.status);
    for(i=0;i<SIZE-1;++i)CHECK(guarded.status.error_message[i]=='x');
    CHECK(guarded.status.error_message[SIZE-1]==0);
    for(i=SIZE;i<sizeof(guarded.status.error_message);++i)
        CHECK(guarded.status.error_message[i]==0);
    for(i=0;i<32;++i)CHECK(guarded.before[i]==0xa5 && guarded.after[i]==0xa5);
    CHECK(guarded.status.now_ms==0xa5a5a5a5u);
    memset(long_text,'L',sizeof(long_text)-1);long_text[sizeof(long_text)-1]=0;
    fatal_text("%s",long_text);
    CHECK(strlen(fm1_doom_error_message)==SIZE-1 && fm1_doom_error_message[SIZE-1]==0);
    snapshot(&guarded.status);CHECK(strlen(guarded.status.error_message)==SIZE-1);
    for(i=SIZE;i<sizeof(guarded.status.error_message);++i)
        CHECK(guarded.status.error_message[i]==0);
    memset(&guarded.status,0xa5,sizeof(guarded.status));
    fatal_text("fault %d: %s",7,"short");snapshot(&guarded.status);
    CHECK(!strcmp(guarded.status.error_message,"fault 7: short"));
    for(i=SIZE;i<sizeof(guarded.status.error_message);++i)
        CHECK(guarded.status.error_message[i]==0);
    fatal_text("%s", "");snapshot(&guarded.status);CHECK(guarded.status.error_message[0]==0);
    CHECK(VirtualFree(storage,0,MEM_RELEASE));
    puts("bounded fatal diagnostic formatting and wire snapshot passed");return 0;
}
'''.replace("SIZE", str(size)).replace("FORMATTING", formatting).replace("COPY", copy)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / "contract.c").write_text(source, encoding="utf-8")
            (directory / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.20)\nproject(target_diagnostics C)\n"
                f'add_executable(contract contract.c "{(ROOT / "src/fm1_doom_printf.c").as_posix()}")\n'
                f'target_include_directories(contract PRIVATE "{(ROOT / "include").as_posix()}")\n'
                "target_compile_definitions(contract PRIVATE FM1_DOOM_PRINTF_HOST_TEST=1 "
                "_CRT_SECURE_NO_WARNINGS)\n", encoding="utf-8")
            commands = (
                [cmake, "-S", str(directory), "-B", str(directory / "build"),
                 "-G", "Visual Studio 17 2022", "-A", "x64"],
                [cmake, "--build", str(directory / "build"), "--config", "Release"],
            )
            for command in commands:
                result = subprocess.run(command, capture_output=True, text=True,
                                        errors="replace", timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            executable = directory / "build/Release/contract.exe"
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            # Only this child faults: the historical 256B copy reads the guard page.
            old = subprocess.run([str(executable), "old-copy"], capture_output=True,
                                 text=True, timeout=10)
            self.assertEqual(old.returncode & 0xffffffff, 0xc0000005)


if __name__ == "__main__":
    unittest.main()
