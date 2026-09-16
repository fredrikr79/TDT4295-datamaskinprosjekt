// Includes
#include "myMain.h"
#include "main.h"
#include "fpgacon.h"
#include "stm32u5xx_hal_conf.h"
#include "transport.h"
#include <string.h>



/* ---- entry point -------------------------------------------------------- */


extern void cli_init(UART_HandleTypeDef *huart);
extern void cli_poll(void);
extern UART_HandleTypeDef huart1; 

void myMain(void)
{

    /* ---INIT--- */
    // Intitialize the fpga communiction
    // if (fpga_init(&ospi_backend) != HAL_OK) {
    //     Error_Handler();
    // }

    cli_init(&huart1);

    /* Infinite loop -- nothing in here may block. Each task does one
     * small step and returns. */
    while (1) {
        // FPGA comm state machine
        //fpga_poll();          /* always first: advances the protocol layer */
        //cli_poll();


    if (ospi_backend.done) {
        transport_hdr_t hdr = {0}; 
        hdr.opcode = 0x01;
        if (ospi_backend.write(&hdr, NULL, 0) != HAL_OK) {
            /* didn't start */
        }
    }

        

        // HID input state machine
        // hid_task();
    }
}


/* ---- interrupt callbacks ------------------------------------------------ */

/* The ONE rising-edge EXTI callback for the whole project (U5 has no
 * HAL_GPIO_EXTI_Callback). Add other EXTI pins here as extra ifs. */
void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == FPGA_READY_Pin) {
        on_fpga_ready_irq();

    }
}