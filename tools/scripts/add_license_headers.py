#!/usr/bin/env python3
"""
Add Apache 2.0 license headers to all source files in the project.

Skips vendored, generated, and third-party code.
"""

import os
import re

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))

APACHE_HEADER_C = """/*
 * Copyright 2026 Nature Sense
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

"""

APACHE_HEADER_HASH = """# Copyright 2026 Nature Sense
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""

APACHE_HEADER_SLASH = """// Copyright 2026 Nature Sense
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

"""

# Patterns for files that already have a license header
LICENSE_PATTERNS = [
    re.compile(r'Copyright \d{4} Nature Sense', re.IGNORECASE),
    re.compile(r'Licensed under the Apache License', re.IGNORECASE),
    re.compile(r'SPDX-License-Identifier', re.IGNORECASE),
]

# Directories to skip entirely
SKIP_DIRS = {
    '.git',
    '.venv',
    'venv',
    '__pycache__',
    '.dart_tool',
    '.pub-cache',
    'build',
    'build-',
    'nbg_output',
    'calib_images',
    'Pods',
    'Flutter',
    'node_modules',
    '.idea',
    '.gradle',
    'meson-private',
    '__CMake_compiler_info__',
}

# Files to skip (vendored, third-party, generated)
SKIP_FILES = {
    # Vendored third-party
    'traps/toolkit/include/nlohmann/json.hpp',
    # Civetweb (MIT licensed)
    'traps/toolkit/src/actors/http-sse/civetweb.c',
    'traps/toolkit/src/actors/http-sse/civetweb.h',
    # Generated
    'apps/ai_trap_manager/pubspec.lock',
    'apps/ai_trap_manager/.metadata',
    'apps/ai_trap_manager/ios/Podfile.lock',
    'apps/ai_trap_manager/macos/Podfile.lock',
    'apps/ai_trap_manager/ios/Flutter/AppFrameworkInfo.plist',
    'apps/ai_trap_manager/ios/Flutter/Debug.xcconfig',
    'apps/ai_trap_manager/ios/Flutter/Release.xcconfig',
    'apps/ai_trap_manager/macos/Flutter/Flutter-Debug.xcconfig',
    'apps/ai_trap_manager/macos/Flutter/Flutter-Release.xcconfig',
    'apps/ai_trap_manager/macos/Flutter/GeneratedPluginRegistrant.swift',
    # Config files (not source code)
    'apps/ai_trap_manager/analysis_options.yaml',
    'apps/ai_trap_manager/pubspec.yaml',
    'apps/ai_trap_manager/ios/Podfile',
    'apps/ai_trap_manager/macos/Podfile',
    # Xcode project files
    'apps/ai_trap_manager/ios/Runner/Info.plist',
    'apps/ai_trap_manager/macos/Runner/Info.plist',
    'apps/ai_trap_manager/ios/Runner/AppDelegate.swift',
    'apps/ai_trap_manager/ios/Runner/SceneDelegate.swift',
    'apps/ai_trap_manager/ios/Runner/Runner-Bridging-Header.h',
    'apps/ai_trap_manager/macos/Runner/AppDelegate.swift',
    'apps/ai_trap_manager/macos/Runner/MainFlutterWindow.swift',
    'apps/ai_trap_manager/ios/Runner/Assets.xcassets/AppIcon.appiconset/Contents.json',
    'apps/ai_trap_manager/ios/Runner/Assets.xcassets/LaunchImage.imageset/Contents.json',
    'apps/ai_trap_manager/macos/Runner/Assets.xcassets/AppIcon.appiconset/Contents.json',
    'apps/ai_trap_manager/ios/Runner/Base.lproj/LaunchScreen.storyboard',
    'apps/ai_trap_manager/ios/Runner/Base.lproj/Main.storyboard',
    'apps/ai_trap_manager/macos/Runner/Base.lproj/MainMenu.xib',
    'apps/ai_trap_manager/ios/Runner.xcodeproj/project.pbxproj',
    'apps/ai_trap_manager/macos/Runner.xcodeproj/project.pbxproj',
    'apps/ai_trap_manager/ios/Runner.xcodeproj/project.xcworkspace/contents.xcworkspacedata',
    'apps/ai_trap_manager/macos/Runner.xcodeproj/project.xcworkspace/contents.xcworkspacedata',
    'apps/ai_trap_manager/ios/Runner.xcodeproj/xcshareddata/xcschemes/ Runner.xcscheme',
    'apps/ai_trap_manager/macos/Runner.xcodeproj/xcshareddata/xcschemes/ Runner.xcscheme',
    'apps/ai_trap_manager/ios/Runner.xcworkspace/contents.xcworkspacedata',
    'apps/ai_trap_manager/macos/Runner.xcworkspace/contents.xcworkspacedata',
    'apps/ai_trap_manager/ios/RunnerTests/RunnerTests.swift',
    'apps/ai_trap_manager/macos/RunnerTests/RunnerTests.swift',
    'apps/ai_trap_manager/test/widget_test.dart',
    # Xcode configs
    'apps/ai_trap_manager/macos/Runner/Configs/AppInfo.xcconfig',
    'apps/ai_trap_manager/macos/Runner/Configs/Debug.xcconfig',
    'apps/ai_trap_manager/macos/Runner/Configs/Release.xcconfig',
    'apps/ai_trap_manager/macos/Runner/Configs/Warnings.xcconfig',
    'apps/ai_trap_manager/macos/Runner/DebugProfile.entitlements',
    'apps/ai_trap_manager/macos/Runner/Release.entitlements',
    # Meson build system generated files
    'traps/toolkit/build/meson-private/sanity_check_for_c.c',
    'traps/toolkit/build/meson-private/sanity_check_for_cpp.cpp',
}

# File extensions and their header style
HEADER_STYLES = {
    '.cpp': APACHE_HEADER_C,
    '.hpp': APACHE_HEADER_C,
    '.h': APACHE_HEADER_C,
    '.c': APACHE_HEADER_C,
    '.py': APACHE_HEADER_HASH,
    '.dart': APACHE_HEADER_SLASH,
    '.swift': APACHE_HEADER_SLASH,
    '.sh': APACHE_HEADER_HASH,
    '.yaml': APACHE_HEADER_HASH,
    '.yml': APACHE_HEADER_HASH,
}

# Meson build files (no extension, but use # comments)
MESON_FILES = {
    'meson.build',
    'meson_options.txt',
}


def should_skip(path):
    """Check if a file should be skipped."""
    rel = os.path.relpath(path, PROJECT_ROOT)

    # Check skip dirs
    parts = rel.split(os.sep)
    for part in parts:
        if part in SKIP_DIRS:
            return True
        if part.startswith('build-') or part == 'build':
            return True

    # Check skip files
    if rel in SKIP_FILES:
        return True

    # Skip files in build directories
    if '/build/' in rel or '/build-' in rel:
        return True

    return False


def has_license(content):
    """Check if file already has a license header."""
    for pattern in LICENSE_PATTERNS:
        if pattern.search(content[:500]):
            return True
    return False


def add_header(filepath):
    """Add license header to a file."""
    rel = os.path.relpath(filepath, PROJECT_ROOT)

    if should_skip(filepath):
        print(f"  SKIP (excluded): {rel}")
        return False

    ext = os.path.splitext(filepath)[1]
    basename = os.path.basename(filepath)

    # Determine header style
    if ext in HEADER_STYLES:
        header = HEADER_STYLES[ext]
    elif basename in MESON_FILES:
        header = APACHE_HEADER_HASH
    else:
        print(f"  SKIP (unknown type): {rel}")
        return False

    with open(filepath, 'r') as f:
        content = f.read()

    if has_license(content):
        print(f"  SKIP (already has license): {rel}")
        return False

    # For scripts with shebang, insert after shebang (must be first line)
    if content.startswith('#!'):
        shebang_end = content.find('\n') + 1
        new_content = content[:shebang_end] + '\n' + header + content[shebang_end:]
    else:
        new_content = header + content

    with open(filepath, 'w') as f:
        f.write(new_content)

    print(f"  ADDED: {rel}")
    return True


def main():
    # Collect all source files
    files_modified = 0
    files_skipped = 0
    files_already = 0

    for root, dirs, files in os.walk(PROJECT_ROOT):
        # Skip hidden directories and build dirs
        dirs[:] = [d for d in dirs if not d.startswith('.') and d not in SKIP_DIRS and not d.startswith('build')]

        for f in files:
            filepath = os.path.join(root, f)
            rel = os.path.relpath(filepath, PROJECT_ROOT)

            # Skip non-source files
            ext = os.path.splitext(f)[1]
            basename = os.path.basename(f)

            if ext not in HEADER_STYLES and basename not in MESON_FILES:
                continue

            if should_skip(filepath):
                files_skipped += 1
                continue

            if add_header(filepath):
                files_modified += 1
            else:
                # Check if it was skipped due to already having license
                if should_skip(filepath):
                    files_skipped += 1
                else:
                    try:
                        with open(filepath, 'r') as fh:
                            if has_license(fh.read()):
                                files_already += 1
                            else:
                                files_skipped += 1
                    except:
                        files_skipped += 1

    print(f"\nDone! {files_modified} files modified, {files_already} already had license, {files_skipped} skipped.")


if __name__ == '__main__':
    main()
