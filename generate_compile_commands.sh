#!/bin/bash

# Script to generate compile_commands.json for Bazel projects
# This helps IDEs like VSCode, CLion, etc. with code completion

set -e

echo "======================================"
echo "Generating compile_commands.json"
echo "======================================"

# Method 1: Using Hedron's Compile Commands Extractor (Recommended)
echo ""
echo "Checking if hedron_compile_commands is configured..."

if grep -q "hedron_compile_commands" WORKSPACE; then
    echo "✓ hedron_compile_commands already configured"
else
    echo "✗ hedron_compile_commands not found in WORKSPACE"
    echo ""
    echo "Adding hedron_compile_commands to WORKSPACE..."
    
    cat >> WORKSPACE << 'EOF'

# Hedron's Compile Commands Extractor for Bazel
# https://github.com/hedronvision/bazel-compile-commands-extractor
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "hedron_compile_commands",
    url = "https://github.com/hedronvision/bazel-compile-commands-extractor/archive/ed994039a951b736091776d677f324b3903ef939.tar.gz",
    strip_prefix = "bazel-compile-commands-extractor-ed994039a951b736091776d677f324b3903ef939",
    sha256 = "085bde6c5212c8c1603595341ffe7133108034808d8c819f8978b2b303afc9e7",
)

load("@hedron_compile_commands//:workspace_setup.bzl", "hedron_compile_commands_setup")
hedron_compile_commands_setup()
EOF
    
    echo "✓ Added hedron_compile_commands to WORKSPACE"
fi

echo ""
echo "Generating compile_commands.json..."
echo "This may take a few minutes on first run..."
echo ""

# Generate compile_commands.json
bazel run @hedron_compile_commands//:refresh_all

echo ""
echo "======================================"
echo "✓ Success!"
echo "======================================"
echo ""
echo "compile_commands.json has been generated in the project root."
echo ""
echo "For VSCode users:"
echo "  Make sure you have the C/C++ extension installed"
echo "  The extension will automatically use compile_commands.json"
echo ""
echo "For CLion users:"
echo "  Go to: File -> Settings -> Build, Execution, Deployment -> Bazel Settings"
echo "  Set 'Compilation Database' to the generated compile_commands.json"
echo ""
