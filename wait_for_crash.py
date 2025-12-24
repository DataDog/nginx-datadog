#!/usr/bin/env python3
"""Wait for nginx crash by tailing docker logs"""

import subprocess
import sys
import time
from datetime import datetime

def main():
    print("=== Waiting for nginx crash ===")
    print(f"Started at: {datetime.now()}")
    print("Watching for: 'Exited on signal' message")
    print()

    # Start tailing logs
    proc = subprocess.Popen(
        ["docker-compose", "logs", "-f", "--tail=10", "nginx"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )

    try:
        line_count = 0
        for line in proc.stdout:
            line_count += 1

            # Print status every 1000 lines
            if line_count % 100000 == 0:
                print(f"[{datetime.now().strftime('%H:%M:%S')}] Monitoring... ({line_count} lines checked)", flush=True)

            # Check for crash
            if "exited on signal" in line.lower():
                print("\n" + "="*50)
                print("*** CRASH DETECTED! ***")
                print("="*50)

                # Wait a moment for logs to flush
                time.sleep(2)

                # Save logs
                timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                log_file = f"crash_logs_{timestamp}.log"

                print(f"\nSaving full logs to: {log_file}")
                subprocess.run(
                    ["docker-compose", "logs", "nginx"],
                    stdout=open(log_file, 'w'),
                    stderr=subprocess.STDOUT
                )

                # Check for core dumps
                print("\nChecking for core dumps:")
                result = subprocess.run(["ls", "-lh", "./core/core"], capture_output=True, text=True)
                if result.returncode == 0:
                    print(result.stdout)
                    print("\nTo analyze with gdb:")
                    print("  docker exec -it nginx-crash-repro-nginx-1 gdb /usr/local/nginx/sbin/nginx /tmp/cores/core")
                else:
                    print("  No core dumps found")
                    print("\nNote: Core dumps are configured in /tmp/cores/ inside the container")
                    print("      and mounted to ./core/ on the host")

                proc.terminate()
                sys.exit(0)

    except KeyboardInterrupt:
        print("\n\nMonitoring stopped by user")
        proc.terminate()
        sys.exit(1)

if __name__ == "__main__":
    main()
