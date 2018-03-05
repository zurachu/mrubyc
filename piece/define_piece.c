#include "define_piece.h"

#include <mrubyc.h>

#include <piece.h>

static void pce_lcd_line(mrb_vm *vm, mrb_value *v, int argc)
{
    pceLCDLine(GET_INT_ARG(1), GET_INT_ARG(2), GET_INT_ARG(3), GET_INT_ARG(4), GET_INT_ARG(5));
}

static void pce_lcd_paint(mrb_vm *vm, mrb_value *v, int argc)
{
    pceLCDPaint(GET_INT_ARG(1), GET_INT_ARG(2), GET_INT_ARG(3), GET_INT_ARG(4), GET_INT_ARG(5));
}

static void pce_lcd_point(mrb_vm *vm, mrb_value *v, int argc)
{
    pceLCDPoint(GET_INT_ARG(1), GET_INT_ARG(2), GET_INT_ARG(3));
}

void define_pceLcd()
{
    mrbc_define_method(0, mrbc_class_object, "lcd_line", pce_lcd_line);
    mrbc_define_method(0, mrbc_class_object, "lcd_paint", pce_lcd_paint);
    mrbc_define_method(0, mrbc_class_object, "lcd_point", pce_lcd_point);
}
