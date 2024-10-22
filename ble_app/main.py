import simplepyble
import time
from datetime import datetime
from collections import deque

# Constants
DEVICE_NAME = "LCS Peripheral"
TARGET_SERVICE_UUID = "430ebad0-5c25-469e-a162-a1c9dc50a8fd"      # Service UUID
TARGET_CHAR_UUID = "430ebad3-5c25-469e-a162-a1c9dc50a8fd"         # Characteristic UUID
GATT_SERVICE_UUID = "00001801-0000-1000-8000-00805f9b34fb"        # Generic Attribute Service
SERVICE_CHANGED_CHAR_UUID = "00002a05-0000-1000-8000-00805f9b34fb"  # Service Changed characteristic

class ThroughputCalculator:
    def __init__(self, window_size=1.0, average_window=10):
        self.window_size = window_size
        self.bytes_in_window = 0
        self.packets_in_window = 0
        self.last_update = time.time()
        self.start_time = time.time()
        self.total_bytes = 0
        self.total_packets = 0
        
        # For moving average calculation
        self.bps_history = deque(maxlen=average_window)
        self.last_print = time.time()
        self.print_interval = 1.0  # Print stats every second
        
    def update(self, data_size):
        current_time = time.time()
        self.bytes_in_window += data_size
        self.packets_in_window += 1
        self.total_bytes += data_size
        self.total_packets += 1
        
        # Calculate window statistics
        if current_time - self.last_update >= self.window_size:
            elapsed = current_time - self.last_update
            bps = (self.bytes_in_window * 8) / elapsed
            self.bps_history.append(bps)
            
            # Reset window counters
            self.bytes_in_window = 0
            self.packets_in_window = 0
            self.last_update = current_time
            
            # Print statistics periodically
            if current_time - self.last_print >= self.print_interval:
                self.print_statistics()
                self.last_print = current_time
                
    def print_statistics(self):
        # Calculate averages
        avg_bps = sum(self.bps_history) / len(self.bps_history) if self.bps_history else 0
        
        # Calculate overall average
        total_time = time.time() - self.start_time
        overall_avg_bps = (self.total_bytes * 8) / total_time
        
        print("\nThroughput Statistics:")
        print(f"Current Average (last {len(self.bps_history)} windows):")
        print(f"  {avg_bps / 1000:.2f} kbps")
        print(f"  {avg_bps / 8 / 1024:.2f} KB/s")
        print("Overall Average:")
        print(f"  {overall_avg_bps / 1000:.2f} kbps")
        print(f"  {overall_avg_bps / 8 / 1024:.2f} KB/s")
        print(f"Total Data:")
        print(f"  {self.total_bytes / 1024:.2f} KB")
        print(f"  {self.total_packets} packets")
        print(f"  Running time: {total_time:.1f} seconds")

def explore_services(peripheral):
    print("\nExploring all services and characteristics:")
    print("===========================================")
    
    services = peripheral.services()
    for service in services:
        print(f"\nService: {service.uuid()}")
        characteristics = service.characteristics()
        
        for characteristic in characteristics:
            print(f"  └── Characteristic: {characteristic.uuid()}")

def setup_notifications(peripheral):
    print("\nSetting up notifications...")
    throughput_calc = ThroughputCalculator(window_size=0.1)  # 100ms windows
    
    def notification_handler(data):
        data_bytes = bytes(data)
        throughput_calc.update(len(data_bytes))
            
    try:
        peripheral.notify(
            TARGET_SERVICE_UUID,
            TARGET_CHAR_UUID,
            notification_handler
        )
        print("Successfully subscribed to notifications")
    except Exception as e:
        print(f"Failed to subscribe: {str(e)}")
        raise

def main():
    try:
        # Get adapter
        adapters = simplepyble.Adapter.get_adapters()
        if not adapters:
            print("No Bluetooth adapters found.")
            return
        
        adapter = adapters[0]
        print(f"Using adapter: {adapter.identifier()}")

        # Scan for devices
        print("Scanning for devices...")
        adapter.scan_for(2500)
        peripherals = adapter.scan_get_results()

        # Find our device
        target_peripheral = None
        for peripheral in peripherals:
            if peripheral.identifier() == DEVICE_NAME:
                target_peripheral = peripheral
                break

        if not target_peripheral:
            print(f"Device '{DEVICE_NAME}' not found.")
            return

        # Connect and setup
        print(f"\nConnecting to {target_peripheral.identifier()}...")
        target_peripheral.connect()
        print("Connected successfully!")

        explore_services(target_peripheral)
        setup_notifications(target_peripheral)
        
        print("\nMonitoring throughput. Press Ctrl+C to exit...")
        while True:
            time.sleep(0.1)

    except KeyboardInterrupt:
        print("\nUser interrupted. Cleaning up...")
    except Exception as e:
        print(f"An error occurred: {str(e)}")
    finally:
        if 'target_peripheral' in locals() and target_peripheral.is_connected():
            try:
                target_peripheral.unsubscribe(TARGET_SERVICE_UUID, TARGET_CHAR_UUID)
            except:
                pass
            target_peripheral.disconnect()
            print("Disconnected from device.")

if __name__ == "__main__":
    main()
