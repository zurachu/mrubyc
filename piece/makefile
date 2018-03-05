# Piece 用 makefile

# 拡張子の定義

.SUFFIXES:
.SUFFIXES:  .o .s .c

# 生成コマンド・オプションのマクロ

CC = pcc33
CFLAGS = -c -g -gp=0x0 -near -O2 -Wall -I..\src -Iinclude
AS = pcc33
ASFLAGS = -c -g -gp=0x0 -near
LD = pcc33
LDFLAGS = -g -ls -lm

MRBC = ..\..\..\build\host\bin\mrbc

# 生成規則

%.c : %.rb
	$(MRBC) -B$(notdir $(basename $<)) $<

.c.o:
	$(CC) $(CFLAGS) $<

.s.o:
	$(AS) $(ASFLAGS) $<

# 構成ファイル・生成ファイルのマクロ

PRGNAME = hello
FILENAME = hello
CAPTION = HelloWorld
OBJS =	hello.o hal.o define_piece.o test.o \
	..\src\libmrubyc.lib

$(PRGNAME).srf : $(OBJS)
	$(LD) $(LDFLAGS) -e$(PRGNAME).srf $(OBJS)

# 依存関係

# フラッシュ書き込みイメージ生成
pex : $(PRGNAME).srf
	ppack -e $(PRGNAME).srf -o$(FILENAME).pex -n$(CAPTION)

# クリーンアップ
clean:
	del $(PRGNAME).srf $(FILENAME).pex $(PRGNAME).sym $(PRGNAME).map *.o

