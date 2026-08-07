import os
import platform
import subprocess
import sys


def run_executable(exe_path: str) -> int:
    path_to_exe = os.path.abspath(exe_path)

    if not os.path.exists(path_to_exe):
        print(f"Error: File not found at '{path_to_exe}'")
        return 1

    system_os = platform.system()

    if system_os == "Linux":
        # Check if target is a Windows executable
        if path_to_exe.lower().endswith(".exe"):
            cmd = ["wine", path_to_exe]
            # Suppress Wine debug logging
            env = os.environ.copy()
            env["WINEDEBUG"] = "-all"
        else:
            # Native Linux binary
            cmd = [path_to_exe]
            env = os.environ.copy()

    elif system_os == "Windows":
        # Native execution on Windows
        cmd = [path_to_exe]
        env = os.environ.copy()

    else:
        # Fallback for macOS or other UNIX platforms
        cmd = ["wine" if path_to_exe.lower().endswith(".exe") else path_to_exe]
        env = os.environ.copy()
        if path_to_exe.lower().endswith(".exe"):
            env["WINEDEBUG"] = "-all"

    # Forward any extra command line arguments passed to this script
    cmd.extend(sys.argv[2:])

    try:
        process = subprocess.run(cmd, env=env)
        exit_code = process.returncode
        print(f"Exit code: {exit_code}")
        return exit_code
    except FileNotFoundError:
        if "wine" in cmd:
            print(
                "Error: 'wine' is not installed or not found in system PATH."
            )
        else:
            print(f"Error: Could not execute '{path_to_exe}'.")
        return 1


if __name__ == "__main__":
    target = (
        sys.argv[1]
        if len(sys.argv) > 1
        else "/home/fenn/Desktop/Vulpine/test/build/main.exe"
    )
    # Execute the target and print its return code, but exit the runner with 0
    run_executable(target)
    sys.exit(0)