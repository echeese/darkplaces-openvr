#uncomment one of these according to your sound driver
#if you use ALSA version 0.9.x
#SND=snd_alsa_0_9.o
#if you use ALSA version 0.5.x
#SND=snd_alsa_0_5.o
#if you use the kernel sound driver or OSS
SND=snd_oss.o

OBJECTS= buildnumber.o cd_linux.o chase.o cl_demo.o cl_input.o cl_main.o cl_parse.o cl_tent.o cmd.o common.o console.o crc.o cvar.o fractalnoise.o gl_draw.o gl_poly.o gl_rmain.o gl_rmisc.o gl_rsurf.o gl_screen.o gl_warp.o host.o host_cmd.o image.o keys.o mathlib.o menu.o model_alias.o model_brush.o model_shared.o model_sprite.o net_bsd.o net_udp.o net_dgrm.o net_loop.o net_main.o pr_cmds.o pr_edict.o pr_exec.o r_light.o r_part.o r_explosion.o sbar.o snd_dma.o snd_mem.o snd_mix.o $(SND) sv_main.o sv_move.o sv_phys.o sv_user.o sv_light.o sys_linux.o transform.o view.o wad.o world.o zone.o vid_shared.o palette.o r_crosshairs.o gl_textures.o gl_models.o r_sprites.o r_modules.o r_explosion.o r_lerpanim.o cl_effects.o r_decals.o protocol.o

OPTIMIZATIONS= -O6 -ffast-math -funroll-loops -fomit-frame-pointer -fexpensive-optimizations
#OPTIMIZATIONS= -O -g

CFLAGS= -Wall -Werror -I/usr/X11R6/include -I/usr/include/glide $(OPTIMIZATIONS)
#CFLAGS= -Wall -Werror -I/usr/X11R6/include -ggdb $(OPTIMIZATIONS)
#LDFLAGS= -L/usr/X11R6/lib -lm -lX11 -lXext -lXIE -lXxf86dga -lXxf86vm -lGL -ldl
LDFLAGS= -L/usr/X11R6/lib -lm -lX11 -lXext -lXIE -lXxf86dga -lXxf86vm -lGL -ldl -lasound

#most people can't build the -3dfx version (-3dfx version needs some updates for new mesa)
all: darkplaces-glx
#all: darkplaces-glx darkplaces-3dfx

.c.o:
	gcc $(CFLAGS) -c $*.c

darkplaces-glx: $(OBJECTS) vid_glx.o
	gcc -o $@ $^ $(LDFLAGS)

darkplaces-3dfx: $(OBJECTS) in_svgalib.o vid_3dfxsvga.o
	gcc -o $@ $^ $(LDFLAGS)


clean:
	-rm -f darkplaces-glx darkplaces-3dfx
	-rm -f vid_glx.o in_svgalib.o vid_3dfxsvga.o $(OBJECTS)

.PHONY: clean
