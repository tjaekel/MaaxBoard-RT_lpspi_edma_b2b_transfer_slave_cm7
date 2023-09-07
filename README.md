# MaaxBoard-RT_lpspi_edma_b2b_transfer_slave_cm7
 MaaxBoard with SPI Slave

## SPI Slave Project
A converted SDK project, to initialize and use a SPI Slave device
on MaaxBoard-RT

## Attention
The LPUART6 is used as Debug UART (see J1 header pins and move from
LPUART1 to LPUART6!

## LPSPI2 Slave Pins
The pins for LPSPI2 Slave are:
* J1, pin 29 = LPSPI2_SDO = MISO
* J1, pin 31 = LPSPI2_SDI = MOSI
* J1, pin  8 = LPSPI2_SCK = SCLK
* J1, pin 10 = LPSPI2_PCS = nSS

