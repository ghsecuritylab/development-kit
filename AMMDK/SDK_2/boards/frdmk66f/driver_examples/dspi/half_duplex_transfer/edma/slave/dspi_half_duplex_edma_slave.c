/*
 * The Clear BSD License
 * Copyright 2017 NXP
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted (subject to the limitations in the disclaimer below) provided
 * that the following conditions are met:
 *
 * o Redistributions of source code must retain the above copyright notice, this list
 *   of conditions and the following disclaimer.
 *
 * o Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 *
 * o Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY THIS LICENSE.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "fsl_debug_console.h"
#include "fsl_dspi_edma.h"
#include "fsl_dmamux.h"
#include "fsl_dspi.h"
#include "board.h"

#include "pin_mux.h"
#include "clock_config.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define EXAMPLE_DSPI_SLAVE_BASEADDR SPI0

#define EXAMPLE_DSPI_SLAVE_DMA_MUX_BASEADDR DMAMUX
#define EXAMPLE_DSPI_SLAVE_DMA_BASEADDR DMA0
#define EXAMPLE_DSPI_SLAVE_DMA_RX_REQUEST_SOURCE kDmaRequestMux0SPI0Rx
#define EXAMPLE_DSPI_SLAVE_DMA_TX_REQUEST_SOURCE kDmaRequestMux0SPI0Tx
/* The slave will receive 64 bytes from master, and then send them back.
 * slave will do this operation in one transimission, so the transfer data size is 128,
 * and slave will store the received data from the address of dataBuff[64] in the
 * first half transimission. In the second half transimission, slave will send them
 * back to the master board.
 */
#define BUFFER_SIZE (192)
#define RX_BUFFER_INDEX (64)
#define TRANSFER_DATA_SIZE (128)

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
/* DSPI user callback */
void DSPI_SlaveUserCallback(SPI_Type *base, dspi_slave_edma_handle_t *handle, status_t status, void *userData);

/*******************************************************************************
 * Variables
 ******************************************************************************/
static uint8_t dataBuff[BUFFER_SIZE] = {0};

dspi_slave_edma_handle_t g_dspi_edma_s_handle;
edma_handle_t dspiEdmaSlaveRxRegToRxDataHandle;
edma_handle_t dspiEdmaSlaveTxDataToTxRegHandle;

static volatile bool slaveFinished = false;
/*******************************************************************************
 * Code
 ******************************************************************************/
void DSPI_SlaveUserCallback(SPI_Type *base, dspi_slave_edma_handle_t *handle, status_t status, void *userData)
{
    slaveFinished = true;
}

int main(void)
{
    uint32_t i = 0;
    dspi_transfer_t xfer = {0};
    dspi_slave_config_t slaveConfig;

    BOARD_InitPins();
    BOARD_BootClockRUN();
    BOARD_InitDebugConsole();
    PRINTF("\r\nThis is slave example for DSPI half-duplex EDMA transfer.\r\n");
    PRINTF("Slave is working....\r\n");

    /* DMA Mux setting and EDMA init */
    uint32_t slaveRxChannel, slaveTxChannel;
    edma_config_t userConfig;

    slaveRxChannel = 0U;
    slaveTxChannel = 1U;

    /* DMA MUX init */
    DMAMUX_Init(EXAMPLE_DSPI_SLAVE_DMA_MUX_BASEADDR);

    DMAMUX_SetSource(EXAMPLE_DSPI_SLAVE_DMA_MUX_BASEADDR, slaveRxChannel,
                     (uint8_t)EXAMPLE_DSPI_SLAVE_DMA_RX_REQUEST_SOURCE);
    DMAMUX_EnableChannel(EXAMPLE_DSPI_SLAVE_DMA_MUX_BASEADDR, slaveRxChannel);

#if (defined EXAMPLE_DSPI_SLAVE_DMA_TX_REQUEST_SOURCE)
    DMAMUX_SetSource(EXAMPLE_DSPI_SLAVE_DMA_MUX_BASEADDR, slaveTxChannel,
                     (uint8_t)EXAMPLE_DSPI_SLAVE_DMA_TX_REQUEST_SOURCE);
    DMAMUX_EnableChannel(EXAMPLE_DSPI_SLAVE_DMA_MUX_BASEADDR, slaveTxChannel);
#endif

    /* EDMA init */
    /*
     * userConfig.enableRoundRobinArbitration = false;
     * userConfig.enableHaltOnError = true;
     * userConfig.enableContinuousLinkMode = false;
     * userConfig.enableDebugMode = false;
     */
    EDMA_GetDefaultConfig(&userConfig);
    EDMA_Init(EXAMPLE_DSPI_SLAVE_DMA_BASEADDR, &userConfig);

    /* Slave config */
    slaveConfig.whichCtar = kDSPI_Ctar0;
    slaveConfig.ctarConfig.bitsPerFrame = 8U;
    slaveConfig.ctarConfig.cpol = kDSPI_ClockPolarityActiveHigh;
    slaveConfig.ctarConfig.cpha = kDSPI_ClockPhaseFirstEdge;
    slaveConfig.enableContinuousSCK = false;
    slaveConfig.enableRxFifoOverWrite = false;
    slaveConfig.enableModifiedTimingFormat = false;
    slaveConfig.samplePoint = kDSPI_SckToSin0Clock;
    DSPI_SlaveInit(EXAMPLE_DSPI_SLAVE_BASEADDR, &slaveConfig);

    /* Set up dspi slave first */
    memset(&(dspiEdmaSlaveRxRegToRxDataHandle), 0, sizeof(dspiEdmaSlaveRxRegToRxDataHandle));
    memset(&(dspiEdmaSlaveTxDataToTxRegHandle), 0, sizeof(dspiEdmaSlaveTxDataToTxRegHandle));
    EDMA_CreateHandle(&(dspiEdmaSlaveRxRegToRxDataHandle), EXAMPLE_DSPI_SLAVE_DMA_BASEADDR, slaveRxChannel);
    EDMA_CreateHandle(&(dspiEdmaSlaveTxDataToTxRegHandle), EXAMPLE_DSPI_SLAVE_DMA_BASEADDR, slaveTxChannel);
    DSPI_SlaveTransferCreateHandleEDMA(EXAMPLE_DSPI_SLAVE_BASEADDR, &g_dspi_edma_s_handle, DSPI_SlaveUserCallback, NULL,
                                       &dspiEdmaSlaveRxRegToRxDataHandle, &dspiEdmaSlaveTxDataToTxRegHandle);

    /* Receive data from master board.*/
    xfer.txData = dataBuff;
    xfer.rxData = &dataBuff[RX_BUFFER_INDEX];
    xfer.dataSize = TRANSFER_DATA_SIZE;
    DSPI_SlaveTransferEDMA(EXAMPLE_DSPI_SLAVE_BASEADDR, &g_dspi_edma_s_handle, &xfer);

    /* Wait for the recieve complete. */
    while (slaveFinished != true)
    {
    }

    PRINTF("\r\nThe received data are:");
    for (i = 0; i < BUFFER_SIZE / 3; i++)
    {
        /* Print 16 data in a line. */
        if ((i & 0x0F) == 0)
        {
            PRINTF("\r\n");
        }
        PRINTF("  0x%02X", dataBuff[i + RX_BUFFER_INDEX]);
    }

    PRINTF("\r\n\r\nSlave transfer completed.");

    DMAMUX_Deinit(EXAMPLE_DSPI_SLAVE_DMA_MUX_BASEADDR);
    EDMA_Deinit(EXAMPLE_DSPI_SLAVE_DMA_BASEADDR);
    DSPI_Deinit(EXAMPLE_DSPI_SLAVE_BASEADDR);

    while (1)
    {
    }
}
