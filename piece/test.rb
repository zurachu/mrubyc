def draw_polygon(color, p)
  i = 1
  while i < p.size do
    lcd_line(0, p[i-1][0], p[i-1][1], p[i][0], p[i][1])
    i += 1
  end
  lcd_line(0, p[i-1][0], p[i-1][1], p[0][0], p[0][1])
end

lcd_paint(3, 0, 0, 128, 88)

p = []
p.push([15, 20])
p.push([60, 60])
p.push([105, 30])
draw_polygon(0, p)
