/************************************************************************/
/* includes                                                             */
/************************************************************************/

#include <asf.h>
#include <string.h>
#include "ili9341.h"
#include "lvgl.h"
#include "touch/touch.h"
#include "arm_math.h"


/************************************************************************/
/* constants and global variables                                       */
/************************************************************************/
#define RAIO 0.508f/2.0f
#define VEL_MAX_KMH  5.0f
#define VEL_MIN_KMH  0.5f
#define RAMP
#define SPEED_UPDATE_T 10
#define MAGNETIC_PIO PIOA
#define MAGNETIC_PIO_ID ID_PIOA
#define MAGNETIC_PIO_IDX 19
#define MAGNETIC_PIO_IDX_MASK (1 << MAGNETIC_PIO_IDX)
float wheel_radius = 0.5;
double bike_velocity = 0.0;
double bike_velocity_previous = 0.0;
double acceleration = 0.0;
volatile int time_since_last_pulse = 0;
double trajectory_travelled_dist = 0.0;
uint64_t trajectory_seconds = 0;
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
lv_obj_t * labelAccel;
lv_obj_t * labelCron;
lv_obj_t * labelDist;
lv_obj_t * labelAvgSpeed;
lv_obj_t * labelPlay;
lv_obj_t * labelPause;
lv_obj_t * labelReset;










SemaphoreHandle_t xMutexLVGL ;
LV_FONT_DECLARE(dseg18);
LV_FONT_DECLARE(dseg24);

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

/************************************************************************/
/* handlers / callbacks                                                 */
/************************************************************************/

static void pulse_callback() {
	time_since_last_pulse = rtt_read_timer_value(RTT);
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	xSemaphoreGiveFromISR(xSemaphoreSpeed,&xHigherPriorityTaskWoken);
	
	
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

	}

    rtc_clear_status(RTC, RTC_SCCR_SECCLR);
    rtc_clear_status(RTC, RTC_SCCR_ALRCLR);
    rtc_clear_status(RTC, RTC_SCCR_ACKCLR);
    rtc_clear_status(RTC, RTC_SCCR_TIMCLR);
    rtc_clear_status(RTC, RTC_SCCR_CALCLR);
    rtc_clear_status(RTC, RTC_SCCR_TDERRCLR);
	/// ...
}
void lv_draw_gui(void) {
	static lv_style_t style;
	lv_style_init(&style);
	lv_style_set_bg_color(&style, lv_color_black());
	lv_style_set_border_width(&style, 0);


	labelClock = lv_label_create(lv_scr_act());
	lv_obj_set_style_text_font(labelClock, &dseg18, LV_STATE_DEFAULT);

	lv_obj_set_style_text_color(labelClock, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelClock, "10:00:00",4);
	lv_obj_align(labelClock, LV_ALIGN_TOP_LEFT, 10, 10);

	labelSpeed = lv_label_create(lv_scr_act());
	lv_obj_set_style_text_font(labelSpeed, &dseg24, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelSpeed, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelSpeed, "VEL: %d", 0);
	lv_obj_align_to(labelSpeed,labelClock,LV_ALIGN_OUT_BOTTOM_LEFT,0,10);
	
	
	lv_obj_t * labelAccelTxt;
	labelAccelTxt = lv_label_create(lv_scr_act());
	lv_obj_set_style_text_font(labelAccelTxt, &dseg24, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelAccelTxt, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text(labelAccelTxt, "ACEL: ");
	lv_obj_align_to(labelAccelTxt,labelSpeed,LV_ALIGN_OUT_BOTTOM_LEFT,0,10);
	
	labelAccel = lv_label_create(lv_scr_act());
	lv_obj_set_style_text_color(labelAccel, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text(labelAccel, "0");
	
	lv_obj_align_to(labelAccel,labelAccelTxt,LV_ALIGN_OUT_RIGHT_MID,0,0);
	lv_obj_t * labelTrajeto;
	labelTrajeto = lv_label_create(lv_scr_act());
	lv_obj_set_style_text_font(labelTrajeto, &dseg24, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelTrajeto, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text(labelTrajeto, "TRAJETO:");
	lv_obj_align(labelTrajeto,LV_ALIGN_CENTER,0,-30);


	labelCron = lv_label_create(lv_scr_act());
	lv_obj_set_style_text_font(labelCron, &dseg18, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelCron, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text(labelCron, "00 00");
	lv_obj_align_to(labelCron,labelAccelTxt,LV_ALIGN_OUT_BOTTOM_LEFT,10,50);
	
	
	labelDist = lv_label_create(lv_scr_act());
	lv_obj_set_style_text_font(labelDist, &dseg18, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelDist, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelDist, "%d M", 0);
	lv_obj_align_to(labelDist,labelCron,LV_ALIGN_OUT_RIGHT_MID,30,0);
	
	
	labelAvgSpeed = lv_label_create(lv_scr_act());
	lv_obj_set_style_text_font(labelAvgSpeed, &dseg24, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelAvgSpeed, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text(labelAvgSpeed, "V MED 0.00");
	lv_obj_align_to(labelAvgSpeed,labelCron,LV_ALIGN_OUT_BOTTOM_LEFT,10,20);
	
	
}

/************************************************************************/
/* TASKS                                                                */
/************************************************************************/

static void task_lcd(void *pvParameters) {
	int px, py;
	xSemaphoreTake( xMutexLVGL, portMAX_DELAY );
	lv_draw_gui();
	xSemaphoreGive( xMutexLVGL );

	for (;;)  {
		xSemaphoreTake( xMutexLVGL, portMAX_DELAY );
	
		
		lv_tick_inc(50);
		lv_task_handler();
		xSemaphoreGive( xMutexLVGL );
		vTaskDelay(50);
	}
}
static void task_clock(void *pvParameters) {
	calendar rtc_initial = {2018, 3, 19, 12, 15, 45 ,1};
	
	RTC_init(RTC, ID_RTC, rtc_initial, RTC_IER_SECEN);
	
	uint32_t current_hour, current_min, current_sec;
	uint32_t cron_hour=0, cron_min=0, cron_sec=0;
	int blink = 1;

	for (;;)  {
		 if (xSemaphoreTake(xSemaphoreClock, 1000)) {
			 rtc_get_time(RTC, &current_hour, &current_min, &current_sec);
			 cron_sec++;
			 trajectory_seconds++;
			 if (cron_sec>=60){
				 cron_sec = 0;
				 cron_min++;
			 }
			 if (cron_min>=60){
				 cron_min = 0;
				 cron_min++;
			 }
			 xSemaphoreTake( xMutexLVGL, portMAX_DELAY );
			 if (blink){
				lv_label_set_text_fmt(labelClock, "%02d:%02d:%02d", current_hour,current_min,current_sec);
				lv_label_set_text_fmt(labelCron, "%02d:%02d", cron_hour,cron_min);
				
	
			 } else {
				lv_label_set_text_fmt(labelClock, "%02d %02d %02d", current_hour,current_min,current_sec);
				lv_label_set_text_fmt(labelCron, "%02d %02d", cron_hour,cron_min);
				
				
			 }
			 xSemaphoreGive( xMutexLVGL );
			 
			 blink = !blink;
			 
		 }
	}
}
static void task_speed(void *pvParameters) {
	double time;
	char* icon;
	const double m_per_rot = (2.0*PI*RAIO);
	double avgSpeed;
	for (;;){
		 if (xSemaphoreTake(xSemaphoreSpeed, 1000)) {
			 trajectory_travelled_dist += m_per_rot;
			 avgSpeed = (trajectory_travelled_dist/trajectory_seconds)*3.6;
			 
			time = ((double)time_since_last_pulse)/2048.0;
			bike_velocity =( (double)(PI * 2.0*3.6*RAIO))/ time;
			acceleration = (bike_velocity-bike_velocity_previous);
			bike_velocity_previous = bike_velocity;
			xSemaphoreTake( xMutexLVGL, portMAX_DELAY );
			if ( acceleration < 0.2 && acceleration > -0.2){
				lv_label_set_text(labelAccel, "0");
			
			} else if (acceleration>0){
				lv_label_set_text(labelAccel, LV_SYMBOL_UP);
				
			} else {
				lv_label_set_text(labelAccel, LV_SYMBOL_DOWN) ;
				
			}
			lv_label_set_text_fmt(labelSpeed,"VEL:%.2f", bike_velocity);
			lv_label_set_text_fmt(labelDist,"%d M", (int)trajectory_travelled_dist);
			lv_label_set_text_fmt(labelAvgSpeed,"V. MED:%.2f", avgSpeed);
			
			
			
			
			xSemaphoreGive( xMutexLVGL );
					
	
			rtt_init(RTT,16); //2048hz
	
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
	//freq 1024hz
	rtt_init(RTT,16);
	pmc_enable_periph_clk(MAGNETIC_PIO_ID);
	pio_set_input(MAGNETIC_PIO, MAGNETIC_PIO_IDX_MASK, PIO_DEFAULT);
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


float kmh_to_hz(float vel, float raio) {
	float f = vel / (2*PI*raio*3.6);
	return(f);
}

static void task_simulador(void *pvParameters) {

	pmc_enable_periph_clk(ID_PIOC);
	pio_set_output(PIOC, PIO_PC31, 1, 0, 0);

	float vel = VEL_MAX_KMH;
	float f;
	int ramp_up = 1;

	while(1){
		pio_clear(PIOC, PIO_PC31);
		delay_ms(1);
		pio_set(PIOC, PIO_PC31);
		#ifdef RAMP
		if (ramp_up) {
			printf("[SIMU] ACELERANDO: %d \n", (int) (10*vel));
			vel += 0.5;
			} else {
			printf("[SIMU] DESACELERANDO: %d \n",  (int) (10*vel));
			vel -= 0.5;
		}

		if (vel >= VEL_MAX_KMH)
		ramp_up = 0;
		else if (vel <= VEL_MIN_KMH)
		ramp_up = 1;
		#endif
		#ifndef RAMP
		vel = 5;
		printf("[SIMU] CONSTANTE: %d \n", (int) (10*vel));
		#endif
		f = kmh_to_hz(vel, RAIO);
		int t = 965*(1.0/f);
		delay_ms(t);
	}
}
int main(void) {
	board_init();
	sysclk_init();
	configure_console();
	configure_magnetic();
	configure_lcd();
	
	ili9341_set_orientation(ILI9341_FLIP_Y|ILI9341_SWITCH_XY);
	
	configure_touch();
	configure_lvgl();
	xSemaphoreClock = xSemaphoreCreateBinary();
	xSemaphoreSpeed = xSemaphoreCreateBinary();
	xMutexLVGL  = xSemaphoreCreateMutex();
	if (xTaskCreate(task_lcd, "LCD", TASK_LCD_STACK_SIZE, NULL, TASK_LCD_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create lcd task\r\n");
	}
	if (xTaskCreate(task_clock, "CLOCK", TASK_LCD_STACK_SIZE, NULL, TASK_LCD_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create clock task\r\n");
	}
	if (xTaskCreate(task_speed, "speed", TASK_LCD_STACK_SIZE, NULL, TASK_LCD_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create clock task\r\n");
	}
	 if (xTaskCreate(task_simulador, "SIMUL", TASK_LCD_STACK_SIZE, NULL, TASK_LCD_STACK_PRIORITY, NULL) != pdPASS) {
		 printf("Failed to create lcd task\r\n");
	 }
	vTaskStartScheduler();
	while(1){ }
}
