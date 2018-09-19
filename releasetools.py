# Copyright (C) 2012 The Android Open Source Project
# Copyright (C) 2012 The CyanogenMod Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Custom OTA commands for aries"""

import common
import os

TARGET_DIR = os.getenv('OUT')
UTILITIES_DIR = os.path.join(TARGET_DIR, 'utilities')

def FullOTA_Assertions(info):
  info.output_zip.write(os.path.join(TARGET_DIR, "modem.bin"), "modem.bin")
  info.output_zip.write(os.path.join(UTILITIES_DIR, "flash_image"), "flash_image")

  info.script.AppendExtra(
        ('package_extract_file("flash_image", "/tmp/flash_image");\n'
         'set_perm(0, 0, 0777, "/tmp/flash_image");'))

  info.script.AppendExtra('package_extract_file("boot.img", "/tmp/boot.img");')
  info.script.AppendExtra('assert(run_program("/tmp/flash_image", "boot", "/tmp/boot.img") == 0);')

  # modem.bin
  info.script.Mount('/radio')
  info.script.AppendExtra('package_extract_file("modem.bin", "/radio/modem.bin");')

def FullOTA_InstallEnd(info):
  # Remove writing boot.img from script (we do it manually)
  info.script.script = [cmd for cmd in info.script.script if not "write_raw_image" in cmd]
