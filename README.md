# ESPClock
ESP8266 based 7 segment clock

# Videos
- Part 1: https://www.youtube.com/watch?v=0H1_9tu25HU
- Part 2: https://www.youtube.com/watch?v=LJ9TrVol3vA

# Hardware used

- ESP-201 as main processor
- 2 x 74HC595 1 driving segments, 1 spare for future additions
- 1 x 74HC4017 for module selection
- 1 x 8xNPN-Array for interfacing the first 595 to the segments
- 2 x 8xPNP-Array for interacing the decade counter to the modules

# Pinout

| PIN | IC | USE |  Note |
|----|----|---|---|
| 12   | 595   | Data  |   |
| 14   |  595  |  SHCP |   |
| 2   | 595   | RESET  |  HIGH on boot |
| 5   | 595   | STCP  |   |
| 13   |  4017  |  CLK |   |
| 15  | 4017   | RESET  | LOW on boot  |
