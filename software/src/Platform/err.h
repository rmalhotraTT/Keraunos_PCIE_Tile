/*
 * @file err.h
 *
 * @brief Error code definitions and logging utilities
 *
 *
 */

#ifndef ERR_H
#define ERR_H

#include <stdint.h>

/* Base offset macros for each driver/module */
#define GRENDEL_ERR_BASE_CCE_DRV (100U)
#define GRENDEL_ERR_BASE_UART_DRV (200U)
#define GRENDEL_ERR_BASE_SMC_DRV (300U)
#define GRENDEL_ERR_BASE_NOC_DRV (400U)
#define GRENDEL_ERR_BASE_GPIO_DRV (500U)

/** grendel_err_t - Error code enumeration */
typedef enum {
  GRENDEL_ERR_OK = 0U,
  GRENDEL_ERR_UNKNOWN,
  GRENDEL_ERR_INVALID_PARAM,
  GRENDEL_ERR_TIMEOUT,
  GRENDEL_ERR_IO,
  GRENDEL_ERR_NOMEM,
  GRENDEL_ERR_NOT_FOUND,
  GRENDEL_ERR_BUSY,
  GRENDEL_ERR_UNSUPPORTED,
  GRENDEL_ERR_PERM,
  GRENDEL_ERR_CCE_DRV_INIT_FAILED = GRENDEL_ERR_BASE_CCE_DRV,
  GRENDEL_ERR_CCE_DRV_CONFIG_INVALID,
  GRENDEL_ERR_CCE_DRV_NOT_READY,
  GRENDEL_ERR_CCE_DRV_FIFO_FULL,
  GRENDEL_ERR_CCE_DRV_FIFO_EMPTY,
  GRENDEL_ERR_CCE_DRV_DMA_TRANSFER_FAIL,
  GRENDEL_ERR_CCE_DRV_INVALID_INSTANCE,
  GRENDEL_ERR_CCE_DRV_COMM_FAILED,
  GRENDEL_ERR_CCE_DRV_NEW_ERROR,
  GRENDEL_ERR_UART_DRV_INIT_FAILED = GRENDEL_ERR_BASE_UART_DRV,
  GRENDEL_ERR_UART_DRV_CONFIG_INVALID,
  GRENDEL_ERR_UART_DRV_TX_FAIL,
  GRENDEL_ERR_UART_DRV_RX_FAILED,
  GRENDEL_ERR_UART_DRV_OVERRUN,
  GRENDEL_ERR_UART_DRV_PARITY,
  GRENDEL_ERR_UART_DRV_FRAMING,
  GRENDEL_ERR_UART_DRV_BREAK,
  GRENDEL_ERR_UART_DRV_FIFO_ERROR,
  GRENDEL_ERR_SMC_DRV_INIT_FAILED = GRENDEL_ERR_BASE_SMC_DRV,
  GRENDEL_ERR_SMC_DRV_CONFIG_INVALID,
  GRENDEL_ERR_SMC_DRV_NOT_READY,
  GRENDEL_ERR_NOC_DRV_INIT_FAILED = GRENDEL_ERR_BASE_NOC_DRV,
  GRENDEL_ERR_NOC_DRV_ROUTE_INVALID,
  GRENDEL_ERR_NOC_DRV_CONGESTION,
  GRENDEL_ERR_GPIO_DRV_INVALID_PIN = GRENDEL_ERR_BASE_GPIO_DRV,
  GRENDEL_ERR_GPIO_DRV_CONFIG_INVALID,
  GRENDEL_ERR_GPIO_DRV_NOT_OUTPUT
} grendel_err_t;

/**
 * @brief Convert error code to human-readable string
 * @param[in] err - Error code
 *
 * @return
 *    Corresponding error message string
 */
static inline const char *grendel_err_to_str(grendel_err_t err) {
  switch (err) {
  case GRENDEL_ERR_OK:
    return "Success - No error";
  case GRENDEL_ERR_UNKNOWN:
    return "Unknown error";
  case GRENDEL_ERR_INVALID_PARAM:
    return "Invalid parameter";
  case GRENDEL_ERR_TIMEOUT:
    return "Timeout";
  case GRENDEL_ERR_IO:
    return "I/O error";
  case GRENDEL_ERR_NOMEM:
    return "Out of memory";
  case GRENDEL_ERR_NOT_FOUND:
    return "Not found";
  case GRENDEL_ERR_BUSY:
    return "Resource busy";
  case GRENDEL_ERR_UNSUPPORTED:
    return "Operation not supported";
  case GRENDEL_ERR_PERM:
    return "Permission denied";
  case GRENDEL_ERR_CCE_DRV_INIT_FAILED:
    return "CCE initialization failed";
  case GRENDEL_ERR_CCE_DRV_CONFIG_INVALID:
    return "CCE configuration invalid";
  case GRENDEL_ERR_CCE_DRV_NOT_READY:
    return "CCE not ready";
  case GRENDEL_ERR_CCE_DRV_FIFO_FULL:
    return "CCE FIFO full";
  case GRENDEL_ERR_CCE_DRV_FIFO_EMPTY:
    return "CCE FIFO empty";
  case GRENDEL_ERR_CCE_DRV_DMA_TRANSFER_FAIL:
    return "CCE DMA transfer failed";
  case GRENDEL_ERR_CCE_DRV_INVALID_INSTANCE:
    return "CCE invalid instance";
  case GRENDEL_ERR_CCE_DRV_COMM_FAILED:
    return "CCE communication failed";
  case GRENDEL_ERR_CCE_DRV_NEW_ERROR:
    return "Testing new error added";
  case GRENDEL_ERR_UART_DRV_INIT_FAILED:
    return "UART initialization failed";
  case GRENDEL_ERR_UART_DRV_CONFIG_INVALID:
    return "UART configuration invalid";
  case GRENDEL_ERR_UART_DRV_TX_FAIL:
    return "UART transmit failed";
  case GRENDEL_ERR_UART_DRV_RX_FAILED:
    return "UART receive failed";
  case GRENDEL_ERR_UART_DRV_OVERRUN:
    return "UART overrun error";
  case GRENDEL_ERR_UART_DRV_PARITY:
    return "UART parity error";
  case GRENDEL_ERR_UART_DRV_FRAMING:
    return "UART framing error";
  case GRENDEL_ERR_UART_DRV_BREAK:
    return "UART break condition";
  case GRENDEL_ERR_UART_DRV_FIFO_ERROR:
    return "UART FIFO error";
  case GRENDEL_ERR_SMC_DRV_INIT_FAILED:
    return "SMC initialization failed";
  case GRENDEL_ERR_SMC_DRV_CONFIG_INVALID:
    return "SMC configuration invalid";
  case GRENDEL_ERR_SMC_DRV_NOT_READY:
    return "SMC not ready";
  case GRENDEL_ERR_NOC_DRV_INIT_FAILED:
    return "NOC initialization failed";
  case GRENDEL_ERR_NOC_DRV_ROUTE_INVALID:
    return "NOC route invalid";
  case GRENDEL_ERR_NOC_DRV_CONGESTION:
    return "NOC congestion detected";
  case GRENDEL_ERR_GPIO_DRV_INVALID_PIN:
    return "GPIO invalid pin number";
  case GRENDEL_ERR_GPIO_DRV_CONFIG_INVALID:
    return "GPIO configuration invalid";
  default:
    return "Unrecognized error code";
  }
}

#endif // ERR_H
