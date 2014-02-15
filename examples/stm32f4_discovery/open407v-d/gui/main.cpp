
#include <xpcc/architecture.hpp>
#include <xpcc/debug/logger.hpp>
#include <xpcc/driver/ui/display/parallel_tft.hpp>
#include <xpcc/driver/ui/display/tft_memory_bus.hpp>
#include <xpcc/ui/display/image.hpp>
#include <xpcc/driver/ui/touchscreen/ads7843.hpp>

#include <xpcc/ui/gui.hpp>

#include "touchscreen_calibrator.hpp"

#include "../../stm32f4_discovery.hpp"
#include "images/bluetooth_12x16.hpp"

// ----------------------------------------------------------------------------
/*
 * Setup UART Logger
 */

// Set the log level
#undef	XPCC_LOG_LEVEL
#define	XPCC_LOG_LEVEL xpcc::log::DEBUG

// Create an IODeviceWrapper around the Uart Peripheral we want to use
xpcc::IODeviceWrapper< Usart2 > loggerDevice;

// Set all four logger streams to use the UART
xpcc::log::Logger xpcc::log::debug(loggerDevice);
xpcc::log::Logger xpcc::log::info(loggerDevice);
xpcc::log::Logger xpcc::log::warning(loggerDevice);
xpcc::log::Logger xpcc::log::error(loggerDevice);


void
initLogger()
{
	GpioOutputA2::connect(Usart2::Tx);
	GpioInputA3::connect(Usart2::Rx);
	Usart2::initialize<defaultSystemClock, 115200>(12);
}

// ----------------------------------------------------------------------------

/* FSMC
 *
 * * Use A16 as Command / Data pin
 * 0x60000000 is the base address for FSMC's first memory bank.
 * When accessing 0x60000000 A16 is low.
 *
 * Why offset +0x20000 for A16?
 * (1 << 16) is 0x10000.
 *
 * But the TftMemoryBus16Bit uses the FSMC in 16 bit mode.
 * Then, according to Table 184 (External memory address) of
 * reference manual (p1317) address HADDR[25:1] >> 1 are issued to
 * the address pins A24:A0.
 * So when writing to offset +((1 << 16) << 1) pin A16 is high.
 */
xpcc::TftMemoryBus16Bit parallelBus(
		(volatile uint16_t *) 0x60000000,
		(volatile uint16_t *) 0x60020000);

xpcc::ParallelTft<xpcc::TftMemoryBus16Bit> tft(parallelBus);

// ----------------------------------------------------------------------------

constexpr uint8_t TP_TOLERANCE = 30;

xpcc::gui::inputQueue input_queue;
xpcc::glcd::Point last_point;

// ----------------------------------------------------------------------------
// Touchscreen

typedef GpioOutputC4 CsTouchscreen;
typedef GpioInputC5  IntTouchscreen;

xpcc::Ads7843<SpiSimpleMaster2, CsTouchscreen, IntTouchscreen> ads7843;
xpcc::TouchscreenCalibrator touchscreen;

typedef GpioOutputD7 CS;

static void
initDisplay()
{

	Fsmc::initialize();
	
	GpioD14::connect(Fsmc::D0);
	GpioD15::connect(Fsmc::D1);
	GpioD0::connect(Fsmc::D2);
	GpioD1::connect(Fsmc::D3);
	GpioE7::connect(Fsmc::D4);
	GpioE8::connect(Fsmc::D5);
	GpioE9::connect(Fsmc::D6);
	GpioE10::connect(Fsmc::D7);
	GpioE11::connect(Fsmc::D8);
	GpioE12::connect(Fsmc::D9);
	GpioE13::connect(Fsmc::D10);
	GpioE14::connect(Fsmc::D11);
	GpioE15::connect(Fsmc::D12);
	GpioD8::connect(Fsmc::D13);
	GpioD9::connect(Fsmc::D14);
	GpioD10::connect(Fsmc::D15);

	GpioD4::connect(Fsmc::Noe);
	GpioD5::connect(Fsmc::Nwe);
	GpioD11::connect(Fsmc::A16);
	

	CS::setOutput();
	CS::reset();
	
	fsmc::NorSram::AsynchronousTiming timing = {
		// read
		15,
		0,
		15,
		
		// write
		15,
		0,
		15,
		
		// bus turn around 
		0
	};
	
	fsmc::NorSram::configureAsynchronousRegion(
			fsmc::NorSram::CHIP_SELECT_1,
			fsmc::NorSram::NO_MULTIPLEX_16BIT,
			fsmc::NorSram::SRAM_ROM,
			fsmc::NorSram::MODE_A,
			timing);
	
	fsmc::NorSram::enableRegion(fsmc::NorSram::CHIP_SELECT_1);

	tft.initialize();
}

static void
initTouchscreen()
{
	CsTouchscreen::setOutput();
	CsTouchscreen::set();
	
	IntTouchscreen::setInput(Gpio::InputType::PullUp);

	GpioOutputB13::connect(SpiSimpleMaster2::Sck);
	GpioInputB14::connect(SpiSimpleMaster2::Miso);
	GpioOutputB15::connect(SpiSimpleMaster2::Mosi);

	SpiSimpleMaster2::initialize<defaultSystemClock, MHz1/2>();
	SpiSimpleMaster2::setDataMode(SpiSimpleMaster2::DataMode::Mode0);

}

// ----------------------------------------------------------------------------
/* screen calibration  */
static void
drawCross(xpcc::GraphicDisplay& display, xpcc::glcd::Point center)
{
	display.setColor(xpcc::glcd::Color::red());
	display.drawLine(center.x - 15, center.y, center.x - 2, center.y);
	display.drawLine(center.x + 2, center.y, center.x + 15, center.y);
	display.drawLine(center.x, center.y - 15, center.x, center.y - 2);
	display.drawLine(center.x, center.y + 2, center.x, center.y + 15);

	display.setColor(xpcc::glcd::Color::white());
	display.drawLine(center.x - 15, center.y + 15, center.x - 7, center.y + 15);
	display.drawLine(center.x - 15, center.y + 7, center.x - 15, center.y + 15);

	display.drawLine(center.x - 15, center.y - 15, center.x - 7, center.y - 15);
	display.drawLine(center.x - 15, center.y - 7, center.x - 15, center.y - 15);

	display.drawLine(center.x + 7, center.y + 15, center.x + 15, center.y + 15);
	display.drawLine(center.x + 15, center.y + 7, center.x + 15, center.y + 15);

	display.drawLine(center.x + 7, center.y - 15, center.x + 15, center.y - 15);
	display.drawLine(center.x + 15, center.y - 15, center.x + 15, center.y - 7);
}

static void
calibrateTouchscreen(xpcc::GraphicDisplay& display, xpcc::glcd::Point *fixed_samples = NULL)
{
	xpcc::glcd::Point calibrationPoint[3] = { { 45, 45 }, { 270, 90 }, { 100, 190 } };
	xpcc::glcd::Point sample[3];
	
	if(!fixed_samples) {
		for (uint8_t i = 0; i < 3; i++)
		{
			display.clear();

			display.setColor(xpcc::glcd::Color::yellow());
			display.setCursor(50, 5);
			display << "Touch crosshair to calibrate";

			drawCross(display, calibrationPoint[i]);
			xpcc::delay_ms(500);

			while (!ads7843.read(&sample[i])) {
				// wait until a valid sample can be taken
			}

			XPCC_LOG_DEBUG << "calibration point: (" << sample[i].x << " | " << sample[i].y << ")" << xpcc::endl;
		}

		touchscreen.calibrate(calibrationPoint, sample);

	} else {
		touchscreen.calibrate(calibrationPoint, fixed_samples);
	}

	display.clear();
}

void
drawPoint(xpcc::GraphicDisplay& display, xpcc::glcd::Point point)
{
	if (point.x < 0 || point.y < 0) {
		return;
	}
	
	display.drawPixel(point.x, point.y);
	display.drawPixel(point.x + 1, point.y);
	display.drawPixel(point.x, point.y + 1);
	display.drawPixel(point.x + 1, point.y + 1);
}

// ----------------------------------------------------------------------------
/* catch touch input */
bool
touchActive()
{
	/*
	 * XPT2046:
	 *
	 * !PENIRQ when not touched:
	 * _____   _   __________   _   _______
	 *      \_/ \_/          \_/ \_/
	 *     |<----->|
	 *		~120us
	 *
	 * !PENIRQ when touched:
	 *
	 * _____|____|______|__|_|_____|___
	 *
	 * random ~100ns peaks
	 *
	 */

	bool m1, m2;

	m1 = !IntTouchscreen::read();
	xpcc::delay_us(130);
	m2 = !IntTouchscreen::read();

	return (m1 || m2);
}

bool
debounceTouch(xpcc::glcd::Point *out, xpcc::glcd::Point *old)
{
	xpcc::glcd::Point raw, point;

	if(touchActive()) {

		if (ads7843.read(&raw)) {

			// translate point according to calibration
			touchscreen.translate(&raw, &point);

			if(abs(point.x - old->x) < TP_TOLERANCE &&
			   abs(point.y - old->y) < TP_TOLERANCE
			  )
			{
				// point is within area of last touch
				*old = point;
				return false;
			}

			// new touch point
			*old = point;
			*out = point;
			return true;
		}
	} else {
		// reset old point so that when touched again you can touch the same spot
		*old = xpcc::glcd::Point(-400, -400);
	}
	return false;
}

void
gatherInput()
{

	xpcc::glcd::Point point;

	if (debounceTouch(&point, &last_point)) {
		xpcc::gui::InputEvent in_ev;

		in_ev.type = xpcc::gui::InputEvent::Type::TOUCH;
		//in_ev.direction = InputEvent::Direction::DOWN;

		in_ev.coord = point;
		//in_ev.x = point.x;
		//in_ev.y = point.y;

		input_queue.push(in_ev);



	}
}

// ----------------------------------------------------------------------------

void
test_callback(const xpcc::gui::InputEvent& ev, xpcc::gui::Widget* w)
{
	// avoid warnings
	(void) ev;
	(void) w;

	LedGreen::toggle();
}


xpcc::gui::ColorPalette colorpalette[xpcc::gui::Color::PALETTE_SIZE] = {
	xpcc::glcd::Color::black(),
	xpcc::glcd::Color::white(),
	xpcc::glcd::Color::gray(),
	xpcc::glcd::Color::red(),
	xpcc::glcd::Color::green(),
	xpcc::glcd::Color::blue(),
	xpcc::glcd::Color::blue(),		// BORDER
	xpcc::glcd::Color::red(),		// TEXT
	xpcc::glcd::Color::black(),		// BACKGROUND
	xpcc::glcd::Color::red(),		// ACTIVATED
	xpcc::glcd::Color::blue(),		// DEACTIVATED

};

/*
 * empirically found calibration points
 */
xpcc::glcd::Point calibration[] = {{3339, 3046},{931, 2428},{2740, 982}};

// ----------------------------------------------------------------------------
MAIN_FUNCTION
{
	defaultSystemClock::enable();


	LedOrange::setOutput(xpcc::Gpio::Low);
	LedGreen::setOutput(xpcc::Gpio::Low);
	LedBlue::setOutput(xpcc::Gpio::Low);

	Button::setInput();

	initLogger();

	XPCC_LOG_DEBUG << "Hello from xpcc gui example!" << xpcc::endl;

	initDisplay();
	initTouchscreen();

	/*
	 * calibrate touchscreen with already found calibration points
	 */

	calibrateTouchscreen(tft, calibration);
	

	/*
	 * manipulate the color palette
	 */

	colorpalette[xpcc::gui::Color::TEXT] = xpcc::glcd::Color::yellow();


	/*
	 * Create a view and some widgets
	 */

	xpcc::gui::View myView(&tft, colorpalette, &input_queue);

	xpcc::gui::ButtonWidget toggleLedButton((char*)"Toggle Green", xpcc::gui::Dimension(100, 50));
	xpcc::gui::ButtonWidget doNothingButton((char*)"Do nothing", xpcc::gui::Dimension(100, 50));


	/*
	 * connect callbacks to widgets
	 */

	toggleLedButton.cb_activate = &test_callback;


	/*
	 * place widgets in view
	 */

	myView.pack(&toggleLedButton, xpcc::glcd::Point(110, 40));
	myView.pack(&doNothingButton, xpcc::glcd::Point(110, 140));


	/*
	 * main loop
	 */
	while(true) {

		gatherInput();
		myView.run();

		/*
		 * display an arbitrary image
		 */
		tft.drawImage(xpcc::glcd::Point(304, 3),xpcc::accessor::asFlash(bitmap::bluetooth_12x16));

	}

	return 0;
}
