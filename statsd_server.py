#!/usr/bin/env python3

import socket
import csv
import datetime
import time
import threading
import sys
import re
from collections import defaultdict

class MetricsServer:
    def __init__(self, host='0.0.0.0', port=8125, csv_file='metrics.csv'):
        self.host = host
        self.port = port
        self.csv_file = csv_file
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((self.host, self.port))
        self.lock = threading.Lock()
        self.last_values = {}

    def parse_metric(self, data):
        try:
            parts = data.strip().split('|')
            name_value = parts[0].split(':')
            if len(name_value) != 2:
                return None
            name, value = name_value[0], int(name_value[1])
            metric_type = parts[1] if len(parts) > 1 else 'g'
            return {'name': name, 'value': value, 'type': metric_type, 'clock_us': time.monotonic_ns() // 1000}
        except:
            return None

    def write_metric(self, metric):
        with self.lock:
            self.last_values[metric['name']] = metric['value']
            with open(self.csv_file, 'a', newline='') as f:
                writer = csv.writer(f)
                writer.writerow([
                    metric['clock_us'],
                    metric['name'],
                    metric['value'],
                    metric['type']
                ])

    def log_status(self):
        while True:
            time.sleep(10)
            with self.lock:
                if self.last_values:
                    pid_metrics = defaultdict(list)
                    for metric_name, value in self.last_values.items():
                        match = re.match(r'(.+)_(\d+)$', metric_name)
                        if match:
                            base_name, pid = match.group(1), match.group(2)
                            pid_metrics[pid].append((base_name, value))
                        else:
                            pid_metrics['N/A'].append((metric_name, value))

                    print(f"\n[{datetime.datetime.now().strftime('%H:%M:%S')}]", file=sys.stderr)
                    print(f"{'PID':<5} {'Metric':<30} {'Value':<12}", file=sys.stderr)
                    print("-" * 50, file=sys.stderr)

                    for pid in sorted(pid_metrics.keys()):
                        metrics_list = pid_metrics[pid]
                        first_metric, first_value = metrics_list[0]
                        print(f"{pid:<5} {first_metric:<30} {first_value:<12}", file=sys.stderr)
                        for metric_name, value in metrics_list[1:]:
                            print(f"{'':>5} {metric_name:<30} {value:<12}", file=sys.stderr)
                        if pid != sorted(pid_metrics.keys())[-1]:
                            print("", file=sys.stderr)

    def run(self):
        print(f"Metrics server listening on {self.host}:{self.port}")
        print(f"Writing to {self.csv_file}")

        with open(self.csv_file, 'w', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(['timestamp', 'name', 'value', 'type'])

        status_thread = threading.Thread(target=self.log_status, daemon=True)
        status_thread.start()

        while True:
            try:
                data, addr = self.sock.recvfrom(1024)
                metric = self.parse_metric(data.decode('iso-8859-1'))
                if metric:
                    self.write_metric(metric)
            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"Error: {e}")

        self.sock.close()

if __name__ == '__main__':
    server = MetricsServer()
    server.run()
