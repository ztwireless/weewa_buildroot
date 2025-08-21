#! /usr/bin/env bash

PWD=`pwd`
SCRIPT_DIR_REL=`dirname $0`
SCRIPT_DIR=`realpath -s $PWD/${SCRIPT_DIR_REL}`
ROOT=`realpath -s $SCRIPT_DIR/..`
FLASH_TOOL=$ROOT/tools/linux/Linux_Upgrade_Tool/Linux_Upgrade_Tool/upgrade_tool
PACK_FIRMWARE_PATH=${ROOT}/tools/linux/Linux_Pack_Firmware/rockdev

# $1: single camera or not
# $2: enable adb or not
# $3: full image or not
# $4: clean all before build
build() {
	# info print
	echo "printing basic info..."
	echo ">>> buildroot dir: $ROOT"
	if [ $1 == 0 ]; then
		echo ">>> build for: imx334/imx678"
	else
		echo ">>> build for: imx586"
	fi
	if [ $2 == 0 ]; then
		echo ">>> adb enabled: false"
	else
		echo ">>> adb disabled: true"
	fi
	if [ $3 == 0 ]; then
		echo ">>> build type: incremental upgrade image"
	else
		echo ">>> build type: full image"
	fi
	if [ $4 == 0 ]; then
		echo ">>> clean all before build: false"
	else
		echo ">>> clean all before build: true"
	fi
	echo ""

	# basic check
	if check_prerequisite $1 $2; then
		echo "check prerequisite ok"
	else
		echo "check prerequisite failed, abort building"
		exit
	fi

	# clean or not
	if [ $4 == 1 ]; then
		echo ""
		echo "perform a clean build, clean all..."
		cd $ROOT
		./build.sh cleanall
		echo "clean done, rebuilding..."
		./build.sh
	fi

	# build weewa projects
	echo ""
	echo "building weewa projects..."
	build_weewa_app

	# build rkscript
	echo ""
	echo "rebuilding rkscript for adb..."
	rm -f $ROOT/buildroot/output/rockchip_rk3588/target/etc/init.d/S50usbdevice
	cd $ROOT/buildroot
	make rkscript-rebuild

	# build firmware
	echo ""
	echo "building firmware..."
	cd $ROOT
	./build.sh

	# generate image
	echo ""
	echo "generate firmware image..."
	if [ $3 == 0 ]; then
		gen_app_firmware
	else
		gen_system_firmware
	fi

	# end
	echo ""
	echo "all done!!"
}

# $1: single camera or not
# $2: enable adb or not
check_prerequisite() {
	echo "check prerequisite..."

	# check weewa projects
	if [ ! -d $ROOT/external/weewa_idle ]; then
		echo "missing weewa_idle project, you should link it in external dir"
		return 1
	else
		echo "checking weewa_idle project...ok"
	fi
	if [ ! -d $ROOT/buildroot/package/rockchip/weewa_idle ]; then
		echo "missing weewa_idle package, you should link it in uildroot/package/rockchip dir"
		return 1
	else
		echo "checking weewa_idle package...ok"
	fi
	if [ ! -d $ROOT/external/rkipc ]; then
		echo "missing rkipc project, you should link it in external dir"
		return 1
	else
		echo "checking rkipc project...ok"
	fi
	if [ ! -d $ROOT/buildroot/package/rockchip/rkipc ]; then
		echo "missing rkipc package, you should link it in uildroot/package/rockchip dir"
		return 1
	else
		echo "checking rkipc package...ok"
	fi
	if [ ! -d $ROOT/external/weewa_daemon ]; then
		echo "missing weewa_daemon project, you should link it in external dir"
		return 1
	else
		echo "checking weewa_daemon project...ok"
	fi
	if [ ! -d $ROOT/buildroot/package/rockchip/weewa_daemon ]; then
		echo "missing weewa_daemon package, you should link it in uildroot/package/rockchip dir"
		return 1
	else
		echo "checking weewa_daemon package...ok"
	fi

	# check drivers
	if [ -f $ROOT/kernel/drivers/media/i2c/imx334.c ]; then
		echo "checking imx334.c...already existed"
	else
		echo "checking imx334.c....copied"
		cp $SCRIPT_DIR/files/imx334.c $ROOT/kernel/drivers/media/i2c
	fi
	if [ -f $ROOT/kernel/drivers/media/i2c/imx586.c ]; then
		echo "checking imx586.c...already existed"
	else
		echo "checking imx586.c....copied"
		cp $SCRIPT_DIR/files/imx586.c $ROOT/kernel/drivers/media/i2c
	fi
	if [ -f $ROOT/kernel/drivers/media/i2c/imx678.c ]; then
		echo "checking imx678.c...already existed"
	else
		echo "checking imx678.c....copied"
		cp $SCRIPT_DIR/files/imx678.c $ROOT/kernel/drivers/media/i2c
	fi

	# check configs
	if [ $1 == 0 ]; then
		cp -f $SCRIPT_DIR/files/for_334_678/rk3588_weewa.config $ROOT/kernel/arch/arm64/configs
	else
		cp -f $SCRIPT_DIR/files/for_586/rk3588_weewa.config $ROOT/kernel/arch/arm64/configs
	fi
	echo "checking rk3588 weewa config...copied"

	# check dts
	if [ $1 == 0 ]; then
		cp -f $SCRIPT_DIR/files/for_334_678/rk3588-weewa-v10-linux.dts $ROOT/kernel/arch/arm64/boot/dts/rockchip
	else
		cp -f $SCRIPT_DIR/files/for_586/rk3588-weewa-v10-linux.dts $ROOT/kernel/arch/arm64/boot/dts/rockchip
	fi
	echo "checking rk3588-weewa-v10-linux.dts...copied"
	if [ -f $ROOT/kernel/arch/arm64/boot/dts/rockchip/rk3588-weewa-v10.dtsi ]; then
		echo "checking rk3588-weewa-v10.dtsi...already existed"
	else
		cp $SCRIPT_DIR/files/rk3588-weewa-v10.dtsi $ROOT/kernel/arch/arm64/boot/dts/rockchip
		echo "checking rk3588-weewa-v10.dtsi...copied"
	fi
	if [ -f $ROOT/kernel/arch/arm64/boot/dts/rockchip/rk3588-weewa-lp4-v10.dtsi ]; then
		echo "checking rk3588-weewa-lp4-v10.dtsi...already existed"
	else
		cp $SCRIPT_DIR/files/rk3588-weewa-lp4-v10.dtsi $ROOT/kernel/arch/arm64/boot/dts/rockchip
		echo "checking rk3588-weewa-lp4-v10.dtsi...copied"
	fi
	if [ -f $ROOT/kernel/arch/arm64/boot/dts/rockchip/rk3588-weewa-cam-v10.dtsi ]; then
		echo "checking rk3588-weewa-cam-v10.dtsi...already existed"
	else
		cp $SCRIPT_DIR/files/rk3588-weewa-cam-v10.dtsi $ROOT/kernel/arch/arm64/boot/dts/rockchip
		echo "checking rk3588-weewa-cam-v10.dtsi...copied"
	fi
	if [ -f $ROOT/kernel/arch/arm64/boot/dts/rockchip/rk3588-weewa-cam-imx586-one.dtsi ]; then
		echo "checking rk3588-weewa-cam-imx586-one.dtsi...already existed"
	else
		cp $SCRIPT_DIR/files/rk3588-weewa-cam-imx586-one.dtsi $ROOT/kernel/arch/arm64/boot/dts/rockchip
		echo "checking rk3588-weewa-cam-imx586-one.dtsi...copied"
	fi

	# check adb enabled or not
	if [ $2 == 0 ]; then
		cp -f $SCRIPT_DIR/files/disable_adb/rkscript.mk $ROOT/buildroot/package/rockchip/rkscript
	else
		cp -f $SCRIPT_DIR/files/enable_adb/rkscript.mk $ROOT/buildroot/package/rockchip/rkscript
	fi
	echo "checking rkscript.mk...copied"

	return 0
}

build_weewa_app() {
	cd $ROOT
	echo -n "46" | source envsetup.sh 
	cd buildroot
	set -x
	echo -e "\033[32m[0] make weewa_idle-dirclean \033[0m"
	make weewa_idle-dirclean
	echo -e "\033[32m[1] weewa_idle-rebuild \033[0m"
	make weewa_idle-rebuild
	echo -e "\033[32m[2] weewa_daemon-dirclean \033[0m"
	make weewa_daemon-dirclean
	echo -e "\033[32m[3] make weewa_daemon-rebuild \033[0m"
	make weewa_daemon-rebuild
	echo -e "\033[32m[4] make weewa_daemon-dirclean \033[0m"
	make rkipc-dirclean
	echo -e "\033[32m[5] make rkipc-rebuild \033[0m"
	make rkipc-rebuild
	echo -e "\033[32m[6] build_weewa_app success \033[0m"
	set +x
}

gen_app_firmware() {
	cd $PACK_FIRMWARE_PATH
	cp $SCRIPT_DIR/files/package-file-app rk3588-package-file
	./mkupdate.sh
	datetime=$(date +"%Y%m%d")

	if [ ! -d $ROOT/firmware ];then
	  mkdir $ROOT/firmware
	fi
	mv update.img $ROOT/firmware/update-app-${datetime}.img
}

gen_system_firmware() {
	cd $PACK_FIRMWARE_PATH
	cp $SCRIPT_DIR/files/package-file-system rk3588-package-file
	./mkupdate.sh
	datetime=$(date +"%Y%m%d")

	if [ ! -d $ROOT/firmware ];then
	  mkdir $ROOT/firmware
	fi
	mv update.img $ROOT/firmware/update-system-${datetime}.img
}

FOR_586=0
ENABLE_ADB=0
FULL_IMG=0
CLEAN=0
for i in "$@"; do
	case $i in
		--586)
	 		FOR_586=1
	 		;;
	 	--adb)
	 		ENABLE_ADB=1
	 		;;
	 	--full)
	 		FULL_IMG=1
	 		;;
	 	--clean)
	 		CLEAN=1
	 		;;
	 	*)
	 		;;
	 esac
done

build $FOR_586 $ENABLE_ADB $FULL_IMG $CLEAN