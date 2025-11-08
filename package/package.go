package rdno_airquality

import (
	denv "github.com/jurgen-kluft/ccode/denv"
	rdno_lcd "github.com/jurgen-kluft/rdno_lcd/package"
	rdno_lvgl "github.com/jurgen-kluft/rdno_lvgl/package"
	rdno_sensors "github.com/jurgen-kluft/rdno_sensors/package"
	rdno_wifi "github.com/jurgen-kluft/rdno_wifi/package"
)

const (
	repo_path = "github.com\\jurgen-kluft"
	repo_name = "rdno_airquality"
)

func GetPackage() *denv.Package {
	wifipkg := rdno_wifi.GetPackage()
	sensorspkg := rdno_sensors.GetPackage()
	lcdpkg := rdno_lcd.GetPackage()
	lvglpkg := rdno_lvgl.GetPackage()

	mainpkg := denv.NewPackage(repo_path, repo_name)
	mainpkg.AddPackage(wifipkg)
	mainpkg.AddPackage(sensorspkg)
	mainpkg.AddPackage(lcdpkg)
	mainpkg.AddPackage(lvglpkg)

	// Setup the main applications
	airquality := denv.SetupCppAppProject(mainpkg, "airquality", "main")
	airquality.AddDependencies(wifipkg.GetMainLib())
	airquality.AddDependencies(sensorspkg.GetMainLib())
	airquality.AddDependencies(lcdpkg.GetMainLib())
	airquality.AddDependencies(lvglpkg.GetMainLib())

	mainpkg.AddMainApp(airquality)

	return mainpkg
}
