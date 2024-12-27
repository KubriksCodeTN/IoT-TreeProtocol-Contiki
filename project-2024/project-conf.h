/*---------------------------------------------------------------------------*/
#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_
/*---------------------------------------------------------------------------*/
#define NBR_TABLE_CONF_MAX_NEIGHBORS 64
#define ENERGEST_CONF_ON              1
/* Disable button shutdown functionality */
#define BUTTON_SENSOR_CONF_ENABLE_SHUTDOWN    0
/*---------------------------------------------------------------------------*/
/* Enable the ROM bootloader */
#define ROM_BOOTLOADER_ENABLE                 1
/*---------------------------------------------------------------------------*/
/* Change to match your configuration */
#define IEEE802154_CONF_PANID            0xABCD
#define RF_CORE_CONF_CHANNEL                 26
#define RF_BLE_CONF_ENABLED                   0
/*---------------------------------------------------------------------------*/
#undef NETSTACK_CONF_RDC
//#define NETSTACK_CONF_RDC nullrdc_driver
#define NETSTACK_CONF_RDC contikimac_driver
/*---------------------------------------------------------------------------*/
/* my defines */
#undef  NETSTACK_CONF_RDC_CHANNEL_CHECK_RATE
/* For some reason in the simulation 16 does better in both RDC and PDR than 
 * the default value of 8 (I assume this is due to schedule learning being
 * easier with 16 as RDC). 2 and 4 behaved poorly as well 
 */
#define NETSTACK_CONF_RDC_CHANNEL_CHECK_RATE 16
/*---------------------------------------------------------------------------*/
#define NULLRDC_CONF_802154_AUTOACK           1
/*---------------------------------------------------------------------------*/
#if CONTIKI_TARGET_ZOUL
/* System clock runs at 32 MHz */
#define SYS_CTRL_CONF_SYS_DIV         SYS_CTRL_CLOCK_CTRL_SYS_DIV_32MHZ

/* IO clock runs at 32 MHz */
#define SYS_CTRL_CONF_IO_DIV          SYS_CTRL_CLOCK_CTRL_IO_DIV_32MHZ

#define NETSTACK_CONF_WITH_IPV6       0

#define CC2538_RF_CONF_CHANNEL        26

#define COFFEE_CONF_SIZE              0

#define LPM_CONF_MAX_PM               LPM_PM0
#endif
/*---------------------------------------------------------------------------*/
#endif /* PROJECT_CONF_H_ */
/*---------------------------------------------------------------------------*/
