/************************************************************************/
/* includes                                                             */
/************************************************************************/

#include <asf.h>
#include <string.h>
#include "ili9341.h"
#include "lvgl.h"
#include "touch/touch.h"

/************************************************************************/
/* constants and global variables                                       */
/************************************************************************/

#define SPEED_UPDATE_T 3
#define MAGNETIC_PIO PIOA
#define MAGNETIC_PIO_ID ID_PIOA
#define MAGNETIC_PIO_IDX 19
#define MAGNETIC_PIO_IDX_MASK (1 << MAGNETIC_PIO_IDX)
float wheel_radius = 0.5;
float angular_velocity = 0.0;
float bike_velocity = 0.0;
float bike_velocity_previous = 0.0;
float acceleration = 0.0;
float time_since_last_pulse = 0.0;
int accumulated_pulses = 0;
float trajectory_travelled_dist = 0.0;
float trajectory_travelled_time = 0.0;
bool  trajectory_stopped = true;

/************************************************************************/
/* LCD / LVGL                                                           */
/************************************************************************/

#define LV_HOR_RES_MAX          (240)
#define LV_VER_RES_MAX          (320)
static lv_disp_draw_buf_t disp_buf;
static lv_color_t buf_1[LV_HOR_RES_MAX * LV_VER_RES_MAX];
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;
lv_obj_t * labelClock;
lv_obj_t * labelSpeed;


/************************************************************************/
/* RTOS                                                                 */
/************************************************************************/

#define TASK_LCD_STACK_SIZE                (1024*6/sizeof(portSTACK_TYPE))
#define TASK_LCD_STACK_PRIORITY            (tskIDLE_PRIORITY)

SemaphoreHandle_t xSemaphoreClock;
SemaphoreHandle_t xSemaphoreSpeed;

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,  signed char *pcTaskName);
extern void vApplicationIdleHook(void);
extern void vApplicationTickHook(void);
extern void vApplicationMallocFailedHook(void);
extern void xPortSysTickHandler(void);
extern void vApplicationStackOverflowHook(xTaskHandle *pxTask, signed char *pcTaskName) {
	printf("stack overflow %x %s\r\n", pxTask, (portCHAR *)pcTaskName);
	for (;;) {	}
}
extern void vApplicationIdleHook(void) { }
extern void vApplicationTickHook(void) { }
extern void vApplicationMallocFailedHook(void) {
	configASSERT( ( volatile void * ) NULL );
}

/************************************************************************/
/* lvgl funcs                                                           */
/************************************************************************/

static void event_handler(lv_event_t * e) {
	lv_event_code_t code = lv_event_get_code(e);
	if(code == LV_EVENT_CLICKED) {
		LV_LOG_USER("Clicked");
	}
	else if(code == LV_EVENT_VALUE_CHANGED) {
		LV_LOG_USER("Toggled");
	}
}

void lv_draw_gui(void) {
	static lv_style_t style;
	lv_style_init(&style);
	lv_style_set_bg_color(&style, lv_color_black());
	lv_style_set_border_width(&style, 0);


	labelClock = lv_label_create(lv_scr_act());
	// lv_obj_set_style_text_font(labelClock, &dseg30, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelClock, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelClock, "10:00:00",4);
	lv_obj_align(labelClock, LV_ALIGN_TOP_MID, 0, 30);

	labelSpeed = lv_label_create(lv_scr_act());
	// lv_obj_set_style_text_font(labelSpeed, &dseg30, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelSpeed, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelSpeed, "Vel: %d", 0);
	lv_obj_align(labelSpeed, LV_ALIGN_TOP_MID, 0, 60);

}

/************************************************************************/
/* handlers / callbacks                                                 */
/************************************************************************/

static void pulse_callback() {
	printf("pulse_callback\r\n");
}

/************************************************************************/
/* TASKS                                                                */
/************************************************************************/

static void task_lcd(void *pvParameters) {
	int px, py;

	lv_draw_gui();

	for (;;)  {
		lv_tick_inc(50);
		lv_task_handler();
		vTaskDelay(50);
	}
}
typedef struct  {
	uint32_t year;
	uint32_t month;
	uint32_t day;
	uint32_t week;
	uint32_t hour;
	uint32_t minute;
	uint32_t second;
} calendar;

void RTC_init(Rtc *rtc, uint32_t id_rtc, calendar t, uint32_t irq_type) {
	pmc_enable_periph_clk(ID_RTC);
	rtc_set_hour_mode(rtc, 0);
	rtc_set_date(rtc, t.year, t.month, t.day, t.week);
	rtc_set_time(rtc, t.hour, t.minute, t.second);
	NVIC_DisableIRQ(id_rtc);
	NVIC_ClearPendingIRQ(id_rtc);
	NVIC_SetPriority(id_rtc, 4);
	NVIC_EnableIRQ(id_rtc);
	rtc_enable_interrupt(rtc,  RTC_IER_SECEN);
	rtc_enable_interrupt(rtc,  RTC_IER_ALREN);
}

void RTC_Handler(void) {
	uint32_t ul_status = rtc_get_status(RTC);
	
	/* seccond tick */
	if ((ul_status & RTC_SR_SEC) == RTC_SR_SEC) {
		// o c�digo para irq de segundo vem aqui
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		xSemaphoreGiveFromISR(xSemaphoreClock,&xHigherPriorityTaskWoken);
		
	}
	
	/* Time or date alarm */
	if ((ul_status & RTC_SR_ALARM) == RTC_SR_ALARM) {
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		xSemaphoreGiveFromISR(xSemaphoreSpeed,&xHigherPriorityTaskWoken);
	}

    rtc_clear_status(RTC, RTC_SCCR_SECCLR);
    rtc_clear_status(RTC, RTC_SCCR_ALRCLR);
    rtc_clear_status(RTC, RTC_SCCR_ACKCLR);
    rtc_clear_status(RTC, RTC_SCCR_TIMCLR);
    rtc_clear_status(RTC, RTC_SCCR_CALCLR);
    rtc_clear_status(RTC, RTC_SCCR_TDERRCLR);
	/// ...
}
static void task_clock(void *pvParameters) {
	
	uint32_t current_hour, current_min, current_sec;


	int blink = 1;

	for (;;)  {
		 if (xSemaphoreTake(xSemaphoreClock, 1000)) {
			 rtc_get_time(RTC, &current_hour, &current_min, &current_sec);
			 if (blink){
				lv_label_set_text_fmt(labelClock, "%02d:%02d:%02d", current_hour,current_min,current_sec);
	
			 } else {
				lv_label_set_text_fmt(labelClock, "%02d %02d %02d", current_hour,current_min,current_sec);
				
			 }
			 blink = !blink;
			 
		 }
	}
}
static void task_speed(void *pvParameters) {
	uint32_t current_hour, current_min, current_sec;
	const max_sec = 60 - SPEED_UPDATE_T;
	rtc_get_time(RTC, &current_hour, &current_min, &current_sec);
	if (current_sec<40){
		rtc_set_time_alarm(RTC, 1, current_hour, 1, current_min, 1, current_sec + SPEED_UPDATE_T);

	} else {
		rtc_set_time_alarm(RTC, 1, current_hour, 1, current_min, 1, SPEED_UPDATE_T - (60-current_sec));
	}
	for (;;){
		if (xSemaphoreTake(xSemaphoreSpeed, 1000)) {
			bike_velocity+=1.2;
			lv_label_set_text_fmt(labelSpeed,"Vel: %f",bike_velocity);
			rtc_get_time(RTC, &current_hour, &current_min, &current_sec);
			if (current_sec<max_sec){
				rtc_set_time_alarm(RTC, 1, current_hour, 1, current_min, 1, current_sec + SPEED_UPDATE_T);

			} else {
				rtc_set_time_alarm(RTC, 1, current_hour, 1, current_min, 1, SPEED_UPDATE_T - (60-current_sec));
			}
		}
	}
}

/************************************************************************/
/* configs                                                              */
/************************************************************************/

static void configure_lcd(void) {
	pio_configure_pin(LCD_SPI_MISO_PIO, LCD_SPI_MISO_FLAGS);  //
	pio_configure_pin(LCD_SPI_MOSI_PIO, LCD_SPI_MOSI_FLAGS);
	pio_configure_pin(LCD_SPI_SPCK_PIO, LCD_SPI_SPCK_FLAGS);
	pio_configure_pin(LCD_SPI_NPCS_PIO, LCD_SPI_NPCS_FLAGS);
	pio_configure_pin(LCD_SPI_RESET_PIO, LCD_SPI_RESET_FLAGS);
	pio_configure_pin(LCD_SPI_CDS_PIO, LCD_SPI_CDS_FLAGS);
	ili9341_init();
	ili9341_backlight_on();
}

static void configure_console(void) {
	const usart_serial_options_t uart_serial_options = {
		.baudrate = USART_SERIAL_EXAMPLE_BAUDRATE,
		.charlength = USART_SERIAL_CHAR_LENGTH,
		.paritytype = USART_SERIAL_PARITY,
		.stopbits = USART_SERIAL_STOP_BIT,
	};
	stdio_serial_init(CONSOLE_UART, &uart_serial_options);
	setbuf(stdout, NULL);
}

static void configure_magnetic(void) {
	pmc_enable_periph_clk(MAGNETIC_PIO_ID);
	pio_configure(MAGNETIC_PIO, PIO_INPUT, MAGNETIC_PIO_IDX_MASK, PIO_DEFAULT | PIO_DEBOUNCE);
	pio_set_debounce_filter(MAGNETIC_PIO, MAGNETIC_PIO_IDX_MASK, 10);
	pio_handler_set(MAGNETIC_PIO, MAGNETIC_PIO_ID, MAGNETIC_PIO_IDX_MASK, PIO_IT_FALL_EDGE, pulse_callback);
	pio_enable_interrupt(MAGNETIC_PIO, MAGNETIC_PIO_IDX_MASK);
	pio_get_interrupt_status(MAGNETIC_PIO);
	NVIC_EnableIRQ(MAGNETIC_PIO_ID);
	NVIC_SetPriority(MAGNETIC_PIO_ID, 4);
}

/************************************************************************/
/* port lvgl                                                            */
/************************************************************************/

void my_flush_cb(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p) {
	ili9341_set_top_left_limit(area->x1, area->y1);   ili9341_set_bottom_right_limit(area->x2, area->y2);
	ili9341_copy_pixels_to_screen(color_p,  (area->x2 + 1 - area->x1) * (area->y2 + 1 - area->y1));
	lv_disp_flush_ready(disp_drv);
}

void my_input_read(lv_indev_drv_t * drv, lv_indev_data_t*data) {
	int px, py, pressed;
	if (readPoint(&px, &py))
		data->state = LV_INDEV_STATE_PRESSED;
	else
		data->state = LV_INDEV_STATE_RELEASED; 
	data->point.x = py;
	data->point.y = 320-px;
}

void configure_lvgl(void) {
	lv_init();
	lv_disp_draw_buf_init(&disp_buf, buf_1, NULL, LV_HOR_RES_MAX * LV_VER_RES_MAX);
	lv_disp_drv_init(&disp_drv);
	disp_drv.draw_buf = &disp_buf;
	disp_drv.flush_cb = my_flush_cb;
	disp_drv.hor_res = LV_HOR_RES_MAX;
	disp_drv.ver_res = LV_VER_RES_MAX;
	lv_disp_t * disp;
	disp = lv_disp_drv_register(&disp_drv);
	lv_indev_drv_init(&indev_drv);
	indev_drv.type = LV_INDEV_TYPE_POINTER;
	indev_drv.read_cb = my_input_read;
	lv_indev_t * my_indev = lv_indev_drv_register(&indev_drv);
}

/************************************************************************/
/* main                                                                 */
/************************************************************************/

int main(void) {
	board_init();
	sysclk_init();
	configure_lcd();
	configure_console();
	configure_magnetic();
	ili9341_set_orientation(ILI9341_FLIP_Y|ILI9341_SWITCH_XY);
	configure_touch();
	configure_lvgl();
	calendar rtc_initial = {2018, 3, 19, 12, 15, 45 ,1};
	RTC_init(RTC, ID_RTC, rtc_initial, RTC_IER_SECEN);
	xSemaphoreClock = xSemaphoreCreateBinary();
	xSemaphoreSpeed = xSemaphoreCreateBinary();
	if (xTaskCreate(task_lcd, "LCD", TASK_LCD_STACK_SIZE, NULL, TASK_LCD_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create lcd task\r\n");
	}
	if (xTaskCreate(task_clock, "CLOCK", TASK_LCD_STACK_SIZE, NULL, TASK_LCD_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create clock task\r\n");
	}
	if (xTaskCreate(task_speed, "speed", TASK_LCD_STACK_SIZE, NULL, TASK_LCD_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create clock task\r\n");
	}
	vTaskStartScheduler();
	while(1){ }
}
