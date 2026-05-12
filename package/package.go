package rairquality

import (
	denv "github.com/jurgen-kluft/ccode/denv"
	rlcd "github.com/jurgen-kluft/rlcd/package"
	rsensors "github.com/jurgen-kluft/rsensors/package"
	rwifi "github.com/jurgen-kluft/rwifi/package"
)

const (
	repo_path = "github.com\\jurgen-kluft"
	repo_name = "rairquality"
)

func GetPackage() *denv.Package {
	wifipkg := rwifi.GetPackage()
	sensorspkg := rsensors.GetPackage()
	lcdpkg := rlcd.GetPackage()

	mainpkg := denv.NewPackage(repo_path, repo_name)
	mainpkg.AddPackage(wifipkg)
	mainpkg.AddPackage(sensorspkg)
	mainpkg.AddPackage(lcdpkg)

	// Setup the main applications
	airquality := denv.SetupCppAppProject(mainpkg, "airquality", "main")
	airquality.AddDependencies(wifipkg.GetMainLib())
	airquality.AddDependencies(sensorspkg.GetMainLib())
	airquality.AddDependency(lcdpkg.GetLibrary("lib_dwo"))

	mainpkg.AddMainApp(airquality)

	return mainpkg
}
