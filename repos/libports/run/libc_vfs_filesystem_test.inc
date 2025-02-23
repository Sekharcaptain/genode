#
# \brief  Test for block-backed file-systems in the libc VFS
# \author Christian Prochaska
# \author Emery Hemingway
# \date   2017-07-31
#

#
# \arg mkfs_cmd
# \arg mkfs_opts
# \arg use_vfs_server
# \arg test_build_components
# \arg test_vfs_config
# \arg test_boot_modules
#

if {[have_board zynq_qemu] || [have_board virt_qemu]} {
	puts "Run script does not support this platform."
	exit 0
}

# use SD on ARM
set use_sd_card_drv [expr [have_board pbxa9]]

# use AHCI on x86
set use_ahci        [expr [have_spec x86] && ![have_spec linux]]

# use vfs_block on Linux
set use_vfs_block   [expr [have_spec linux] || [have_board rpi3]]

if {[catch { exec which $mkfs_cmd } ]} {
	puts stderr "Error: $mkfs_cmd not installed, aborting test"; exit }

if {[expr ![have_include "power_on/qemu"] && !$use_vfs_block]} {
	puts "\nPlease setup your native sd or hard drive. Remove this fail stop";
	puts "check when you have prepared your native environment.\n";
	exit 0
}

#
# Build
#

set build_components { test/libc_vfs }

lappend build_components {*}$test_build_components

lappend_if $use_sd_card_drv build_components drivers/platform
lappend_if $use_sd_card_drv build_components drivers/sd_card/pl180

source ${genode_dir}/repos/base/run/platform_drv.inc
append_platform_drv_build_components

build $build_components

create_boot_directory

set depot_pkgs "[depot_user]/src/[base_src] [depot_user]/src/init"

lappend_if $use_ahci       depot_pkgs [depot_user]/src/ahci_drv
lappend_if $use_vfs_block  depot_pkgs [depot_user]/src/vfs_block
lappend_if $use_vfs_server depot_pkgs [depot_user]/src/vfs
lappend_if $use_vfs_block  depot_pkgs [depot_user]/src/vfs_import

import_from_depot {*}$depot_pkgs

#
# Generate config
#

set config {
<config verbose="yes">
	<parent-provides>
		<service name="CPU"/>
		<service name="IO_MEM"/>
		<service name="IO_PORT"/>
		<service name="IRQ"/>
		<service name="LOG"/>
		<service name="PD"/>
		<service name="RM"/>
		<service name="ROM"/>
	</parent-provides>
	<default-route>
		<any-service> <parent/> <any-child/> </any-service>
	</default-route>
	<default caps="100"/>
	<start name="timer">
		<resource name="RAM" quantum="1M"/>
		<provides> <service name="Timer"/> </provides>
	</start>
	<start name="test-libc_vfs" caps="200">
		<resource name="RAM" quantum="10M"/>
		<config>
			<iterations value="1"/>}
append_if [have_include "power_on/qemu"] config {
			<write-read size="1M" buffer_size="8K"/>}
append config {
			<libc stdout="/dev/log" stderr="/dev/log"/>
			<vfs>
				<dir name="dev"> <log/> </dir>}
if {$use_vfs_server} {
	append config "<fs/>"
} else {
	append config $test_vfs_config
}
append config {
			</vfs>
		</config>
	</start>}

append_platform_drv_config

append_if $use_ahci config {
	<start name="ahci_drv">
		<resource name="RAM" quantum="10M" />
		<provides><service name="Block" /></provides>
		<route>
			<any-service> <parent /> <any-child /></any-service>
		</route>
		<config>
			<policy label_prefix="test-libc_vfs" device="0" writeable="yes"/>
			<policy label_prefix="vfs" device="0" writeable="yes"/>
		</config>
	</start>}

append_if $use_sd_card_drv config {
	<start name="platform_drv">
		<resource name="RAM" quantum="1M"/>
		<provides><service name="Platform"/></provides>
		<config>
			<device name="pl180" type="arm,pl18x">
				<io_mem   address="0x10005000" size="0x1000"/>
			</device>
			<policy label="pl180_sd_card_drv -> ">
				<device name="pl180"/>
			</policy>
		</config>
		<route>
			<any-service> <parent/> </any-service>
		</route>
	</start>
	<start name="pl180_sd_card_drv">
		<resource name="RAM" quantum="1M" />
		<provides><service name="Block"/></provides>
	</start>
}

append_if $use_vfs_block config {
	<start name="vfs_block" caps="150">
		<resource name="RAM" quantum="68M" />
		<provides><service name="Block"/></provides>
		<config>
			<vfs>
				<ram/>
				<import>
					<rom name="test.hda"/>
				</import>
			</vfs>
			<default-policy file="/test.hda" block_size="512"
			                writeable="yes"/>
		</config>
	</start>}

append_if $use_vfs_server config "
	<start name=\"vfs\" caps=\"200\">
		<resource name=\"RAM\" quantum=\"12M\" />
		<provides><service name=\"File_system\"/></provides>
		<config>
			<vfs>
				<dir name=\"dev\"> <log/> </dir>}
				$test_vfs_config
			</vfs>
			<policy label_prefix=\"test-libc_vfs\" root=\"/\" writeable=\"yes\"/>
		</config>
	</start>"

append config {
</config>
}

install_config $config

#
# Prepare hard-disk image for test (used by Qemu or in RAM)
#

set disk_image "bin/test.hda"
set cmd "dd if=/dev/zero of=$disk_image bs=1024 count=65536"
puts "creating disk image: $cmd"
catch { exec sh -c $cmd }

set cmd "$mkfs_cmd $mkfs_opts $disk_image"
puts "formating disk: $cmd"
catch { exec sh -c $cmd }

#
# Boot modules
#

# generic modules
append boot_modules {
	libc.lib.so vfs.lib.so test-libc_vfs
}

lappend boot_modules {*}$test_boot_modules

lappend_if $use_sd_card_drv  boot_modules platform_drv
lappend_if $use_sd_card_drv  boot_modules pl180_sd_card_drv
lappend_if $use_vfs_block    boot_modules test.hda

append_platform_drv_boot_modules

build_boot_image $boot_modules

puts "#### boot_modules: $boot_modules"

#
# Qemu
#
append qemu_args "  -nographic "
append_if $use_ahci        qemu_args " -drive id=disk,file=$disk_image,format=raw,if=none -device ahci,id=ahci -device ide-drive,drive=disk,bus=ahci.0 -boot d"
append_if $use_sd_card_drv qemu_args " -drive file=$disk_image,format=raw,if=sd,cache=writeback "

run_genode_until ".*child \"test-libc_vfs\" exited with exit value 0.*" 90

exec rm -f $disk_image

# vi: set ft=tcl :
