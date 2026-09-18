// Includes
#include "myMain.h"
#include "main.h"
#include "transport.h"
#include "cli.h"
#include <string.h>
#include "fpga.h"

extern UART_HandleTypeDef huart1;

/* ---- entry point -------------------------------------------------------- */

void myMain(void)
{
    /* ---INIT--- */
    // Intitialize the fpga communiction
    // if (fpga_init(&ospi_backend) != HAL_OK) {
    //     Error_Handler();
    // }

    // init backend
    transport_init(BACKEND);
    cli_init(&huart1);

    /* Infinite loop -- nothing in here may block. Each task does one
     * small step and returns. */
    while (1) {
        // FPGA comm state machine
        //fpga_poll();          /* always first: advances the protocol layer */
        cli_poll();

        // HID input state machine
        // hid_task();
    }
}


/* ---- interrupt callbacks ------------------------------------------------ */

/* The ONE rising-edge EXTI callback for the whole project (U5 has no
 * HAL_GPIO_EXTI_Callback). Add other EXTI pins here as extra ifs.
 * No UART in here: the CLI only counts, and prints from cli_poll(). */
void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == FPGA_READY_Pin) {
        on_fpga_ready_irq();
        cli_fpga_ready_irq();
    }
}