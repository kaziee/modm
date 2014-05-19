#include "label.hpp"
#include <xpcc/ui/gui/colorpalette.hpp>

void xpcc::gui::Label::draw(AbstractView* view)
{
	if(view == NULL)
		return;

	// output device of view
	xpcc::GraphicDisplay* out = &view->display();

	// color palette of view
	ColorPalette *cp = this->color_palette;

	out->setColor(cp[xpcc::gui::Color::TEXT]);

	if(this->font.isValid())
	{
		out->setFont(&(this->font));
	}
	out->setCursor(this->position.x, this->position.y);
	*out << this->label;
}
