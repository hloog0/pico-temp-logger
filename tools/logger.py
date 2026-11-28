import serial
import sys
import time

if len(sys.argv) < 3:
    print("usage: python3 tools/logger.py <port> <file.csv>")
    sys.exit()
else:
    with open(sys.argv[2], "a") as f:
        counter = 0
        while True:
            try:
                ser = serial.Serial(sys.argv[1], timeout=2)
                while True:
                    current_read = ser.readline()
                    decoded_read = current_read.decode(errors = "replace")
                    decoded_read = decoded_read.strip()
                    if not decoded_read:
                        continue
                    f.write(f"{time.time():.3f},{decoded_read}\n")
                    f.flush()
                    counter += 1
                    if counter % 60 == 0:
                        print(decoded_read)
            except (serial.SerialException, OSError) as e:
                print(e)
                time.sleep(5)