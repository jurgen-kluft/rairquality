package rairquality

import (
	denv "github.com/jurgen-kluft/go-ide/denv"
	rhome "github.com/jurgen-kluft/rhome/package"
	rlcd "github.com/jurgen-kluft/rlcd/package"
	rsensors "github.com/jurgen-kluft/rsensors/package"
	rwifi "github.com/jurgen-kluft/rwifi/package"
)

const (
	repo_path = "github.com/jurgen-kluft"
	repo_name = "rairquality"
)

func GetPackage() *denv.Package {
	wifipkg := rwifi.GetPackage()
	sensorspkg := rsensors.GetPackage()
	lcdpkg := rlcd.GetPackage()
	homepkg := rhome.GetPackage()

	mainpkg := denv.NewPackage(repo_path, repo_name)
	mainpkg.AddPackage(wifipkg)
	mainpkg.AddPackage(sensorspkg)
	mainpkg.AddPackage(lcdpkg)
	mainpkg.AddPackage(homepkg)

	// Setup the main applications
	airquality := denv.SetupCppAppProject(mainpkg, "airquality", "main")
	airquality.AddDependencies(wifipkg.GetMainLib())
	airquality.AddDependencies(homepkg.GetMainLib())
	airquality.AddDependency(sensorspkg.GetLibrary("library_bh1750"))
	airquality.AddDependency(sensorspkg.GetLibrary("library_bme280"))
	airquality.AddDependency(sensorspkg.GetLibrary("library_scd41"))
	airquality.AddDependency(sensorspkg.GetLibrary("library_rd03d"))
	airquality.AddDependency(lcdpkg.GetLibrary("library_wcs"))

	mainpkg.AddMainApp(airquality)

	return mainpkg
}
