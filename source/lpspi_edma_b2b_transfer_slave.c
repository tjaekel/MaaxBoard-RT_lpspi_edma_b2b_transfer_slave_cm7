/*
 * Copyright 2017 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "fsl_lpspi.h"
#include "pin_mux.h"
#include "board.h"
#include "fsl_lpspi_edma.h"
#if defined(FSL_FEATURE_SOC_DMAMUX_COUNT) && FSL_FEATURE_SOC_DMAMUX_COUNT
#include "fsl_dmamux.h"
#endif

#include "fsl_common.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
//#define RECEIVE_ONLY			/* this is the only option which works: SPI slave Rx gets all correct */
//#define TRANSMIT_ONLY			/* strange: not working properly */
#define BIDIRECTIONAL			/* fails in the same way: just the first 4 bytes are correct, on both side! */
/* VERY STRANGE:
 * if we probe, just a small cap, on one of these signals: PCS0, SCLK, MISO (= SDO) - it works
 * BIDIRECTIONAL: max. 17500 KHz : faster results in Master MISO = SDO signal wrong
 * RECEIVE_ONLY:  max. 47000 KHz : SPI Slave just receives (MOSI = SDI) only
 * let SDO open and it works up to 47000 KHz as Slave Receiver, even in BIDIRECTIONAL approach
 * Remark: cross-talk on cable! or cross-talk on my Master MCU board!
 */

/* Slave related */
#define EXAMPLE_LPSPI_SLAVE_BASEADDR              	(LPSPI2)
#define EXAMPLE_LPSPI_SLAVE_DMA_MUX_BASE          	(DMAMUX0)
#define EXAMPLE_LPSPI_SLAVE_DMA_RX_REQUEST_SOURCE 	kDmaRequestMuxLPSPI2Rx
#define EXAMPLE_LPSPI_SLAVE_DMA_TX_REQUEST_SOURCE 	kDmaRequestMuxLPSPI2Tx
#define EXAMPLE_LPSPI_SLAVE_DMA_BASE              	(DMA0)
#define EXAMPLE_LPSPI_SLAVE_DMA_RX_CHANNEL        	2U
#define EXAMPLE_LPSPI_SLAVE_DMA_TX_CHANNEL        	3U

#define EXAMPLE_LPSPI_SLAVE_PCS_FOR_INIT     		(kLPSPI_Pcs0)
#define EXAMPLE_LPSPI_SLAVE_PCS_FOR_TRANSFER 		(kLPSPI_SlavePcs0)
#define TRANSFER_SIZE 								16	//64U /* Transfer dataSize */

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
/* LPSPI user callback */
void LPSPI_SlaveUserCallback(LPSPI_Type *base, lpspi_slave_edma_handle_t *handle, status_t status, void *userData);

/*******************************************************************************
 * Variables
 ******************************************************************************/
AT_NONCACHEABLE_SECTION_INIT(uint8_t slaveRxData[TRANSFER_SIZE]) = {0};
AT_NONCACHEABLE_SECTION_INIT(uint8_t slaveTxData[TRANSFER_SIZE]) = {0};

AT_NONCACHEABLE_SECTION(lpspi_slave_edma_handle_t g_s_edma_handle);
edma_handle_t lpspiEdmaSlaveRxRegToRxDataHandle;
edma_handle_t lpspiEdmaSlaveTxDataToTxRegHandle;

#if (defined(DEMO_EDMA_HAS_CHANNEL_CONFIG) && DEMO_EDMA_HAS_CHANNEL_CONFIG)
extern edma_config_t userConfig;
#else
edma_config_t userConfig = {0};
#endif

volatile bool isTransferCompleted = false;

/*******************************************************************************
 * Code
 ******************************************************************************/

void LPSPI_SlaveUserCallback(LPSPI_Type *base, lpspi_slave_edma_handle_t *handle, status_t status, void *userData)
{
    if (status == kStatus_Success)
    {
        PRINTF("This is LPSPI slave edma transfer completed callback.\r\n\r\n");
    }

    isTransferCompleted = true;
}

/*!
 * @brief Main function
 */
int main(void)
{
    uint32_t i;
    lpspi_slave_config_t slaveConfig;
    lpspi_transfer_t slaveXfer;

    BOARD_ConfigMPU();
    BOARD_InitPins();
    BOARD_BootClockRUN();
    BOARD_InitDebugConsole();

    PRINTF("\r\nLPSPI board to board edma example.\r\n");

/*DMA Mux setting and EDMA init*/
#if defined(FSL_FEATURE_SOC_DMAMUX_COUNT) && FSL_FEATURE_SOC_DMAMUX_COUNT
    /* DMA MUX init*/
    DMAMUX_Init(EXAMPLE_LPSPI_SLAVE_DMA_MUX_BASE);

    DMAMUX_SetSource(EXAMPLE_LPSPI_SLAVE_DMA_MUX_BASE, EXAMPLE_LPSPI_SLAVE_DMA_RX_CHANNEL,
                     EXAMPLE_LPSPI_SLAVE_DMA_RX_REQUEST_SOURCE);
    DMAMUX_EnableChannel(EXAMPLE_LPSPI_SLAVE_DMA_MUX_BASE, EXAMPLE_LPSPI_SLAVE_DMA_RX_CHANNEL);

    DMAMUX_SetSource(EXAMPLE_LPSPI_SLAVE_DMA_MUX_BASE, EXAMPLE_LPSPI_SLAVE_DMA_TX_CHANNEL,
                     EXAMPLE_LPSPI_SLAVE_DMA_TX_REQUEST_SOURCE);
    DMAMUX_EnableChannel(EXAMPLE_LPSPI_SLAVE_DMA_MUX_BASE, EXAMPLE_LPSPI_SLAVE_DMA_TX_CHANNEL);
#endif

    /* EDMA init*/
#if (!defined(DEMO_EDMA_HAS_CHANNEL_CONFIG) || (defined(DEMO_EDMA_HAS_CHANNEL_CONFIG) && !DEMO_EDMA_HAS_CHANNEL_CONFIG))
    /*
     * userConfig.enableRoundRobinArbitration = false;
     * userConfig.enableHaltOnError = true;
     * userConfig.enableContinuousLinkMode = false;
     * userConfig.enableDebugMode = false;
     */
    EDMA_GetDefaultConfig(&userConfig);
#endif
    EDMA_Init(EXAMPLE_LPSPI_SLAVE_DMA_BASE, &userConfig);

    /*Slave config*/
    LPSPI_SlaveGetDefaultConfig(&slaveConfig);
    slaveConfig.whichPcs = EXAMPLE_LPSPI_SLAVE_PCS_FOR_INIT;

    LPSPI_SlaveInit(EXAMPLE_LPSPI_SLAVE_BASEADDR, &slaveConfig);

    /*Set up slave EDMA. */
    memset(&(lpspiEdmaSlaveRxRegToRxDataHandle), 0, sizeof(lpspiEdmaSlaveRxRegToRxDataHandle));
    memset(&(lpspiEdmaSlaveTxDataToTxRegHandle), 0, sizeof(lpspiEdmaSlaveTxDataToTxRegHandle));

    EDMA_CreateHandle(&(lpspiEdmaSlaveRxRegToRxDataHandle), EXAMPLE_LPSPI_SLAVE_DMA_BASE,
                      EXAMPLE_LPSPI_SLAVE_DMA_RX_CHANNEL);
    EDMA_CreateHandle(&(lpspiEdmaSlaveTxDataToTxRegHandle), EXAMPLE_LPSPI_SLAVE_DMA_BASE,
                      EXAMPLE_LPSPI_SLAVE_DMA_TX_CHANNEL);
#if defined(FSL_FEATURE_EDMA_HAS_CHANNEL_MUX) && FSL_FEATURE_EDMA_HAS_CHANNEL_MUX
    EDMA_SetChannelMux(EXAMPLE_LPSPI_SLAVE_DMA_BASE, EXAMPLE_LPSPI_SLAVE_DMA_TX_CHANNEL,
                       DEMO_LPSPI_TRANSMIT_EDMA_CHANNEL);
    EDMA_SetChannelMux(EXAMPLE_LPSPI_SLAVE_DMA_BASE, EXAMPLE_LPSPI_SLAVE_DMA_RX_CHANNEL,
                       DEMO_LPSPI_RECEIVE_EDMA_CHANNEL);
#endif
    LPSPI_SlaveTransferCreateHandleEDMA(EXAMPLE_LPSPI_SLAVE_BASEADDR, &g_s_edma_handle, LPSPI_SlaveUserCallback, NULL,
                                        &lpspiEdmaSlaveRxRegToRxDataHandle, &lpspiEdmaSlaveTxDataToTxRegHandle);

    while (1)
    {
        PRINTF("Slave example is running...\r\n");

        /* Reset the receive buffer */
        for (i = 0U; i < TRANSFER_SIZE; i++)
        {
            slaveRxData[i] = 0U;
        }

#if 0
        /* Set slave transfer ready to receive data */
        isTransferCompleted = false;

        slaveXfer.txData      = NULL;
        slaveXfer.rxData      = slaveRxData;
        slaveXfer.dataSize    = TRANSFER_SIZE;
        slaveXfer.configFlags = EXAMPLE_LPSPI_SLAVE_PCS_FOR_TRANSFER | kLPSPI_SlaveByteSwap;

        LPSPI_SlaveTransferEDMA(EXAMPLE_LPSPI_SLAVE_BASEADDR, &g_s_edma_handle, &slaveXfer);

        while (!isTransferCompleted)
        {
        }
#endif

        /* Set slave transfer ready to send back data */
        isTransferCompleted = false;

        for (i = 0U; i < TRANSFER_SIZE; i++)
        {
            slaveTxData[i] = 0x30U + i;
        }

#ifdef RECEIVE_ONLY
        slaveXfer.txData      = NULL;
        slaveXfer.rxData      = slaveRxData;
#endif
#ifdef TRANSMIT_ONLY
        slaveXfer.txData      = slaveTxData;
        slaveXfer.rxData      = NULL;
#endif
#ifdef BIDIRECTIONAL
        slaveXfer.txData      = slaveTxData;
        slaveXfer.rxData      = slaveRxData;
#endif
        slaveXfer.dataSize    = TRANSFER_SIZE;
        slaveXfer.configFlags = EXAMPLE_LPSPI_SLAVE_PCS_FOR_TRANSFER | kLPSPI_SlaveByteSwap;

        LPSPI_SlaveTransferEDMA(EXAMPLE_LPSPI_SLAVE_BASEADDR, &g_s_edma_handle, &slaveXfer);

        while (!isTransferCompleted)
        {
        }

        /* Print out receive buffer */
        PRINTF("Slave receive:");
        for (i = 0; i < TRANSFER_SIZE; i++)
        {
            /* Print 16 numbers in a line */
            if ((i & 0x0FU) == 0U)
            {
                PRINTF("\r\n    ");
            }
            PRINTF(" %02X", slaveRxData[i]);
        }
        PRINTF("\r\n");
    }
}
