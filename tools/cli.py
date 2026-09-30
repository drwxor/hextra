import argparse
import sys
import subprocess
import shutil
import subprocess

def main():
    parser = argparse.ArgumentParser(description="Development helper for Uorix", add_help=False)

    parser.add_argument("-b", "--build", action="store_true")
    parser.add_argument("-r", "--replace", action="store_true")
    parser.add_argument("-R", "--run", action="store_true")
    parser.add_argument("-i", "--iso", action="store_true")
    parser.add_argument("-I", "--iso-run", action="store_true")
    parser.add_argument("-h", "--help", action="store_true")

    args = parser.parse_args()

    help_text = f"""Usage: {sys.argv[0]} [options]

Options:
  -b, --build       Run build script (build.py)
  -r, --replace     Run disk replacement script (replace.py)
  -R, --run         Run QEMU boot script (run.py)
  -I, --iso-run     Run QEMU boot script with iso (iso-run.py)
  -i, --iso         Create an .iso file containing the system (iso.py)
  -h, --help        Show this help"""

    if args.help or not any([args.build, args.replace, args.run, args.iso, args.iso_run]):
        print(help_text)
        sys.exit(0)

    if args.build:
        print("==> Calling build script...")
        subprocess.run([sys.executable, "tools/build.py"])

    if args.iso:
        print("==> Calling iso build script...")
        subprocess.run([sys.executable, "tools/iso.py"])

    if args.replace:
        print("==> Calling replace script...")
        subprocess.run([sys.executable, "tools/replace.py"])

    if args.run:
        print("==> Calling run script...")
        subprocess.run([sys.executable, "tools/run.py"])

    if args.iso_run:
        print("==> Calling iso run script...")
        subprocess.run([sys.executable, "tools/iso-run.py"])

if __name__ == "__main__":
    main()
