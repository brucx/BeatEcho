#!/usr/bin/env python3
"""A small static regression guard. This is NOT electrical validation or an SDK build."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
header=(ROOT/'firmware/main/board.hpp').read_text()
keys=re.search(r'keys\[8\]\s*=\s*\{(.*?)\}',header,re.S)
assert keys and list(map(int,re.findall(r'GPIO_NUM_(\d+)',keys.group(1)))) == [2,47,38,41,1,6,7,48]
expected={'encoder_a':17,'encoder_b':16,'encoder_press':18,'power':8,'led':12,'status':42,
          'spk_bclk':14,'spk_ws':13,'spk_data':15}
for name,gpio in expected.items():
    assert re.search(rf'\b{name}\s*=\s*GPIO_NUM_{gpio}\b',header), name
sdk=(ROOT/'firmware/sdkconfig.defaults').read_text()
assert 'CONFIG_FREERTOS_HZ=1000' in sdk
assert 'CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y' in sdk
assert 'CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y' in sdk
impl=(ROOT/'firmware/main/board.cpp').read_text()
assert 'input.mode=GPIO_MODE_DISABLE' in impl
assert 'input.pull_up_en=GPIO_PULLUP_DISABLE' in impl
assert impl.index('e=safe_commands()') < impl.index('e=gpio_set_level(power,1)')
assert 'CONFIG_BE_POWER_SETTLE_MS' in impl
assert 'rmt_symbol_word_t symbols_[121]' in header
assert 'GPIO_NUM_0' not in header and 'GPIO_NUM_19' not in header and 'GPIO_NUM_20' not in header
print('PASS: static pin/configuration guards (not target build, electrical validation, or HIL)')
