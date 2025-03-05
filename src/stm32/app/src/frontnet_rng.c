// True Random Hardware Generator (RNG)
// Generate true random numbers using the TRNG available on STM32 chips
//
// See also
// - STM32F405 datasheet: https://www.st.com/resource/en/datasheet/DM00037051.pdf
// - Application note: https://www.st.com/resource/en/application_note/an4230-random-number-generation-validation-using-nist-statistical-test-suite-for-stm32-microcontrollers-stmicroelectronics.pdf
// - Performance metrics: https://www.st.com/content/ccc/resource/training/technical/product_training/group0/22/32/23/22/be/bc/46/5a/STM32G4-Security-Random_Number_Generator_RNG/files/STM32G4-Security-Random_Number_Generator_RNG.pdf/_jcr_content/translations/en.STM32G4-Security-Random_Number_Generator_RNG.pdf

#include "frontnet_rng.h"

#include "FreeRTOS.h"
#include "task.h"

#include "stm32fxxx.h"

void frontnetRNGInit() {
    RCC_AHB2PeriphResetCmd(RCC_AHB2Periph_RNG, DISABLE);
    RCC_AHB2PeriphClockCmd(RCC_AHB2Periph_RNG, ENABLE);
    RNG_Cmd(ENABLE);
}

bool frontnetRNGGetRandomU32(uint32_t *value) {
    if (RNG_GetFlagStatus(RNG_FLAG_DRDY) != SET || RNG_GetFlagStatus(RNG_FLAG_CECS) == SET || RNG_GetFlagStatus(RNG_FLAG_SECS) == SET) {
        return false;
    }

    *value = RNG_GetRandomNumber();
    return true;
}
