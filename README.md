# MiSTer_TTY_Scraper
Image Scraper for MiSTer with TTGO T8 ESP32 and ILI9341

This is designed for a TTGO T8 I had laying around attached to a ILI9341 320 x 240 Display. It needs a controller with SPIs for SD Card and Display along with WiFi.

This uses the uPNG library from elanthis [https://github.com/elanthis/upng/edit/master/README]

This utility uses the existing TTY2OLED script to listen for new cores being loaded. For new cores will attempt to display image using the following priority.
1. Raw image file from SD Card
2. PNG file from SD Card (will convert and save the raw image to SD Card first)
3. Scrape from ArcadeDB. This will save both the PNG and raw image to SD Card

Both the Title and In Game images are scraped and displayed, alternating every 10 seconds. Defines in the source can be changed to try and scrape Marquees, Cabinets and Flyers but these may fail depending on size. These images will not be converted to raw and displayed do to memory limitations, they will just be saved as PNGs to SD Card.

If you want to add your own images, eg for non-Arcade cores, create a folder on the SD Card with the core name and copy 2 PNGs into it, 1 called title.png and the other called ingame.png.  These should be already resized close to 320x240 in order not to cause memory problems. They do not need to be exact and they will be scaled when converted to raw.

WiFi details need to be added to the credentials.txt file which needs to be copied to the root of the SD Card.

![Screenshot](https://github.com/dave18/MiSTer_TTY_Scraper/blob/main/PXL_20240921_095211510.jpg)
