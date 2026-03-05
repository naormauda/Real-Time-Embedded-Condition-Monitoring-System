#!/usr/bin/env python3
"""
Training Data Collection Tool for Smart Safe Anomaly Detection

This script connects to the NUCLEO-H563ZI board via serial, captures
feature extraction output in real-time, and allows users to label scenarios
(NORMAL, VIBRATION, TAMPERING) for building the training dataset.

Usage:
    python3 collect_training_data.py --port COM3 --baudrate 115200
    
    Then follow the on-screen prompts to collect labeled data.

Output:
    Creates CSV files with features and labels for model training:
    - training_data_normal.csv
    - training_data_vibration.csv
    - training_data_tampering.csv
    - training_data_combined.csv (all scenarios combined)
"""

import argparse
import serial
import sys
import os
import csv
import time
from datetime import datetime
from collections import defaultdict
import re

# ANSI color codes for terminal output
class Colors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    YELLOW = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'


class TrainingDataCollector:
    """Collects and labels training data from smart safe sensors"""

    def __init__(self, port, baudrate=115200, timeout=1.0):
        """
        Initialize serial connection to NUCLEO board

        Args:
            port: Serial port (e.g., 'COM3' on Windows, '/dev/ttyUSB0' on Linux)
            baudrate: Serial baud rate (default 115200)
            timeout: Serial read timeout in seconds
        """
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.ser = None
        self.data_buffer = defaultdict(list)
        self.sample_count = defaultdict(int)
        self.current_label = None
        self.running = False

    def connect(self):
        """Establish serial connection"""
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                timeout=self.timeout,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                bytesize=serial.EIGHTBITS
            )
            print(f"{Colors.OKGREEN}✓ Connected to {self.port} @ {self.baudrate} baud{Colors.ENDC}")
            time.sleep(0.5)  # Wait for board to stabilize
            return True
        except serial.SerialException as e:
            print(f"{Colors.FAIL}✗ Failed to connect: {e}{Colors.ENDC}")
            return False

    def disconnect(self):
        """Close serial connection"""
        if self.ser and self.ser.is_open:
            self.ser.close()
            print(f"{Colors.OKGREEN}✓ Disconnected{Colors.ENDC}")

    def parse_feature_line(self, line):
        """
        Parse feature extraction output line

        Expected format:
        [FE] FE: X[u=0.5 v=1.2 r=1.3 p=5.0] Y[u=-0.2 v=0.2 r=0.3 p=0.8] Z[...]

        Returns:
            List of 12 floats [x_mean, x_var, x_rms, x_peak, ...] or None if parse fails
        """
        try:
            # Pattern to match: X[u=(...) v=(...) r=(...) p=(...)]
            pattern = r'X\[u=([-\d.]+)\s+v=([-\d.]+)\s+r=([-\d.]+)\s+p=([-\d.]+)\]\s+' \
                      r'Y\[u=([-\d.]+)\s+v=([-\d.]+)\s+r=([-\d.]+)\s+p=([-\d.]+)\]\s+' \
                      r'Z\[u=([-\d.]+)\s+v=([-\d.]+)\s+r=([-\d.]+)\s+p=([-\d.]+)\]'

            match = re.search(pattern, line)
            if match:
                features = [float(x) for x in match.groups()]
                return features
            return None
        except (ValueError, AttributeError):
            return None

    def display_menu(self):
        """Display main menu"""
        print(f"\n{Colors.HEADER}{Colors.BOLD}=== Smart Safe Training Data Collector ==={Colors.ENDC}\n")
        print("Select a scenario to collect data for:")
        print(f"  {Colors.OKGREEN}[1]{Colors.ENDC} NORMAL      - Device at rest (no motion)")
        print(f"  {Colors.YELLOW}[2]{Colors.ENDC} VIBRATION   - Gentle vibration/movement")
        print(f"  {Colors.FAIL}[3]{Colors.ENDC} TAMPERING   - Rapid shaking/attempts to open")
        print(f"  {Colors.OKCYAN}[4]{Colors.ENDC} View collected samples")
        print(f"  {Colors.OKCYAN}[5]{Colors.ENDC} Export and combine datasets")
        print(f"  {Colors.FAIL}[Q]{Colors.ENDC} Quit\n")

    def collect_scenario(self, label, duration_sec=30):
        """
        Collect training data for a specific scenario

        Args:
            label: Scenario label ('NORMAL', 'VIBRATION', 'TAMPERING')
            duration_sec: How long to collect data (default 30 seconds)
        """
        self.current_label = label
        self.running = True

        print(f"\n{Colors.BOLD}Collecting {label} data...{Colors.ENDC}")
        print(f"Duration: {duration_sec} seconds")
        print(f"Press Ctrl+C to stop early\n")

        start_time = time.time()
        feature_count = 0

        try:
            while self.running and (time.time() - start_time) < duration_sec:
                if self.ser and self.ser.in_waiting > 0:
                    line = self.ser.readline().decode('utf-8', errors='ignore').strip()

                    if '[FE]' in line:
                        features = self.parse_feature_line(line)
                        if features:
                            # Add label and timestamp
                            sample = {
                                'timestamp': datetime.now().isoformat(),
                                'label': label,
                                'features': features
                            }
                            self.data_buffer[label].append(sample)
                            feature_count += 1
                            self.sample_count[label] += 1

                            # Show progress
                            elapsed = int(time.time() - start_time)
                            bar_length = 30
                            progress = min(int((elapsed / duration_sec) * bar_length), bar_length)
                            bar = '█' * progress + '░' * (bar_length - progress)
                            print(f"\r[{bar}] {elapsed}/{duration_sec}s | Samples: {feature_count}", end='', flush=True)

                    elif '[ML]' in line:
                        # Also capture ML output for reference
                        pass

        except KeyboardInterrupt:
            print("\n\nStopped early by user")

        self.running = False
        print(f"\n\n{Colors.OKGREEN}✓ Collected {feature_count} samples of {label}{Colors.ENDC}")
        self.current_label = None

    def display_statistics(self):
        """Display statistics of collected data"""
        print(f"\n{Colors.HEADER}{Colors.BOLD}=== Collected Data Statistics ==={Colors.ENDC}\n")

        total_samples = sum(self.sample_count.values())
        if total_samples == 0:
            print(f"{Colors.YELLOW}No data collected yet{Colors.ENDC}\n")
            return

        for label in ['NORMAL', 'VIBRATION', 'TAMPERING']:
            count = self.sample_count[label]
            if count > 0:
                percentage = (count / total_samples) * 100
                print(f"  {label:12} : {count:4} samples ({percentage:5.1f}%)")

        print(f"\n  {'Total':12} : {total_samples:4} samples")
        print()

    def export_to_csv(self, output_dir='training_data'):
        """
        Export collected data to CSV files

        Args:
            output_dir: Directory to save CSV files
        """
        if not self.data_buffer:
            print(f"{Colors.YELLOW}No data to export{Colors.ENDC}\n")
            return

        # Create output directory
        os.makedirs(output_dir, exist_ok=True)

        # Feature names
        feature_names = [
            'x_mean', 'x_var', 'x_rms', 'x_peak',
            'y_mean', 'y_var', 'y_rms', 'y_peak',
            'z_mean', 'z_var', 'z_rms', 'z_peak'
        ]

        print(f"\n{Colors.HEADER}{Colors.BOLD}=== Exporting Datasets ==={Colors.ENDC}\n")

        # Export individual scenario CSVs
        for label in ['NORMAL', 'VIBRATION', 'TAMPERING']:
            if label in self.data_buffer and self.data_buffer[label]:
                filename = os.path.join(output_dir, f'training_data_{label.lower()}.csv')

                with open(filename, 'w', newline='') as f:
                    writer = csv.writer(f)
                    # Write header
                    writer.writerow(['timestamp', 'label'] + feature_names)
                    # Write data
                    for sample in self.data_buffer[label]:
                        row = [sample['timestamp'], sample['label']] + sample['features']
                        writer.writerow(row)

                count = len(self.data_buffer[label])
                print(f"{Colors.OKGREEN}✓{Colors.ENDC} {filename:45} ({count} samples)")

        # Export combined dataset
        combined_filename = os.path.join(output_dir, 'training_data_combined.csv')
        all_samples = []
        for label in ['NORMAL', 'VIBRATION', 'TAMPERING']:
            if label in self.data_buffer:
                all_samples.extend(self.data_buffer[label])

        if all_samples:
            with open(combined_filename, 'w', newline='') as f:
                writer = csv.writer(f)
                writer.writerow(['timestamp', 'label'] + feature_names)
                for sample in all_samples:
                    row = [sample['timestamp'], sample['label']] + sample['features']
                    writer.writerow(row)

            print(f"{Colors.OKGREEN}✓{Colors.ENDC} {combined_filename:45} ({len(all_samples)} samples)")

        print(f"\n{Colors.OKGREEN}✓ Export complete{Colors.ENDC}\n")

    def run(self):
        """Main interactive loop"""
        if not self.connect():
            return

        print(f"\n{Colors.OKCYAN}Waiting for feature extraction output...{Colors.ENDC}")
        print(f"{Colors.OKCYAN}(System sends [FE] lines every ~1 second){Colors.ENDC}\n")

        # Wait for first feature line to verify connection
        start_wait = time.time()
        while (time.time() - start_wait) < 5.0:
            if self.ser and self.ser.in_waiting > 0:
                line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    print(f"  {Colors.OKCYAN}Received: {line[:60]}...{Colors.ENDC}")
                if '[FE]' in line:
                    print(f"{Colors.OKGREEN}✓ Feature extraction confirmed{Colors.ENDC}\n")
                    break
        else:
            print(f"{Colors.YELLOW}⚠ No feature output detected. Verify serial connection and board is running.{Colors.ENDC}\n")

        # Main menu loop
        while True:
            self.display_menu()
            choice = input(f"{Colors.BOLD}Choice: {Colors.ENDC}").strip().upper()

            if choice == '1':
                self.collect_scenario('NORMAL', duration_sec=30)
            elif choice == '2':
                self.collect_scenario('VIBRATION', duration_sec=30)
            elif choice == '3':
                self.collect_scenario('TAMPERING', duration_sec=30)
            elif choice == '4':
                self.display_statistics()
            elif choice == '5':
                self.export_to_csv()
            elif choice == 'Q':
                print(f"\n{Colors.OKCYAN}Exporting final dataset before exit...{Colors.ENDC}")
                self.export_to_csv()
                break
            else:
                print(f"{Colors.FAIL}Invalid choice{Colors.ENDC}")

        self.disconnect()


def main():
    parser = argparse.ArgumentParser(
        description='Collect training data for Smart Safe anomaly detection'
    )
    parser.add_argument('--port', required=True, help='Serial port (e.g., COM3, /dev/ttyUSB0)')
    parser.add_argument('--baudrate', type=int, default=115200, help='Serial baud rate')

    args = parser.parse_args()

    collector = TrainingDataCollector(args.port, args.baudrate)
    collector.run()


if __name__ == '__main__':
    main()
