# Homebrew cask for GT4Recomp for Mac (this repository is its own tap):
#   brew tap vgs499p/gt4-recomp-mac https://github.com/vgs499p/gt4-recomp-mac
#   brew install --cask gt4recomp
# Bring your own Gran Turismo 4 (USA) v2.00 disc image; the app never includes it.
cask "gt4recomp" do
  version "1.2.5-mac.1"
  sha256 "4fc26ec6f2dd6bcc2ab08dfd61c407fb06641a5d91930c754877b23999956ff4"

  url "https://github.com/vgs499p/gt4-recomp-mac/releases/download/v#{version}/GT4Recomp-#{version}-macos-arm64.zip"
  name "GT4Recomp"
  desc "Static recompilation of Gran Turismo 4 (bring your own disc image)"
  homepage "https://github.com/vgs499p/gt4-recomp-mac"

  depends_on arch: :arm64
  depends_on macos: ">= :sonoma"

  app "GT4Recomp.app"

  # The app is signed ad hoc, not notarized: without this, macOS refuses to open it until it is allowed in
  # System Settings > Privacy & Security.
  postflight do
    system_command "/usr/bin/xattr", args: ["-dr", "com.apple.quarantine", "#{appdir}/GT4Recomp.app"]
  end

  zap trash: "~/Library/Application Support/GT4Recomp"
end
