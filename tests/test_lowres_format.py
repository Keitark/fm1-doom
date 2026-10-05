import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from make_lowres_engine import normalize_printf_formats


class LowresFormatTests(unittest.TestCase):
    def test_configuration_binder_names(self):
        for name in ("joystick_physical_button", "key_multi_msgplayer", "chatmacro"):
            source = f'M_snprintf(name, sizeof(name), "{name}%i", i);'
            expected = f'M_snprintf(name, sizeof(name), "{name}%d", i);'
            self.assertEqual(normalize_printf_formats(source), expected)

    def test_error_width_precision_and_length(self):
        source = 'I_Error("bad %7i %03.2i %+*.*lli %hhi %li %ji %zi %ti %2$*3$.*4$li", n);'
        expected = 'I_Error("bad %7d %03.2d %+*.*lld %hhd %ld %jd %zd %td %2$*3$.*4$ld", n);'
        self.assertEqual(normalize_printf_formats(source), expected)

    def test_percent_escapes_and_multiple_conversions(self):
        source = 'printf("%%i %%%%i %%%i %i%% %s %u", n);'
        expected = 'printf("%%i %%%%i %%%d %d%% %s %u", n);'
        self.assertEqual(normalize_printf_formats(source), expected)

    def test_scanf_base_detection_unchanged(self):
        for call in ("scanf", "fscanf", "sscanf", "vsscanf", "__isoc99_sscanf"):
            source = f'if ({call}(strparm, ("%i %03i %li"), &parm)) printf("%i", parm);'
            expected = f'if ({call}(strparm, ("%i %03i %li"), &parm)) printf("%d", parm);'
            self.assertEqual(normalize_printf_formats(source), expected)

    def test_comments_characters_and_escaped_quotes(self):
        source = '// printf("%i");\n/* "%i" */ char c = \'%\'; printf("\\\"%i\\\"\\n", n);'
        expected = '// printf("%i");\n/* "%i" */ char c = \'%\'; printf("\\\"%d\\\"\\n", n);'
        self.assertEqual(normalize_printf_formats(source), expected)


if __name__ == "__main__":
    unittest.main()
