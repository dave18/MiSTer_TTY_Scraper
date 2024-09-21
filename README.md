# MiSTer_TTY_Scraper
Image Scraper for MiSTer with TTGO T8 ESP32 and ILI9341

This uses the uPNG library from elanthis [https://github.com/elanthis/upng/edit/master/README]

This utility uses the existing TTY2OLED script to listen for new cores being loaded. For new cores will attempt to display image using the following priority.
1. Raw image file from SD Card
2. PNG file from SD Card (will convert and save the raw image to SD Card first)
3. Scrape from ArcadeDB. This will save both the PNG and raw image to SD Card

Both the Title and In Game images are scraped and displayed, alternating every 10 seconds. Defines in the source can be changed to try and scrape Marquees, Cabinets and Flyers but these may fail depending on size. These images will not be converted to raw and displayed do to memory limitations, they will just be saved as PNGs to SD Card.

![Screenshot](https://github.com/dave18/MiSTer_TTY_Scraper/blob/main/PXL_20240921_095211510.jpg)
