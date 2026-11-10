import serial
import statistics
import collections

ser = serial.Serial('/dev/tty.usbmodem21301', timeout = 2)
timestamps = []
gaps = []
n_gaps = 100
unusual_gap_positions = []
EXPECTED_US = 1000000

while len(timestamps) <= n_gaps:
    current_read = ser.readline()
    decoded_read = current_read.decode(errors = "replace")
    decoded_read = decoded_read.strip().split(',')

    if len(decoded_read) == 3:
        # catch bad reads
        try:
            n = int(decoded_read[0])
        except ValueError:
            print("[bad reading, skipped]")
            continue
        print(f"good reading! no. {len(timestamps)}", end = "\r")
        timestamps.append(n)

print()

# compute gaps
for i in range(1, len(timestamps)):
    difference = timestamps[i]-timestamps[i-1]
    gaps.append(difference)
    if difference != EXPECTED_US:
        unusual_gap_positions.append(i)

print(f"Gaps: {len(gaps)}")
print(collections.Counter(gaps))
print(f'Min: {min(gaps)} | Max: {max(gaps)} | Mean: {statistics.fmean(gaps)} | Standard Deviation: {statistics.stdev(gaps)}')
print(f"Unusual gaps at: {unusual_gap_positions}")
