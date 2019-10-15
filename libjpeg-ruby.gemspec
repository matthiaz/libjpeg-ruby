
lib = File.expand_path("../lib", __FILE__)
$LOAD_PATH.unshift(lib) unless $LOAD_PATH.include?(lib)
require "jpeg/version"

Gem::Specification.new do |spec|
  spec.name          = "libjpeg-ruby"
  spec.version       = JPEG::VERSION
  spec.authors       = ["Hiroshi Kuwagata"]
  spec.email         = ["kgt9221@gmail.com"]

  spec.summary       = %q{libjpeg interface for ruby}
  spec.description   = %q{libjpeg interface for ruby}
  spec.homepage      = "https://github.com/kwgt/libjpeg-ruby"
  spec.license       = "MIT"

  if spec.respond_to?(:metadata)
    spec.metadata["homepage_uri"] = spec.homepage
  else
    raise "RubyGems 2.0 or newer is required to protect against " \
      "public gem pushes."
  end

  spec.files         = Dir.chdir(File.expand_path('..', __FILE__)) do
    `git ls-files -z`.split("\x0").reject { |f|
      f.match(%r{^(test|spec|features)/})
    }
  end

  spec.bindir        = "exe"
  spec.executables   = spec.files.grep(%r{^exe/}) { |f| File.basename(f) }
  spec.extensions    = ["ext/jpeg/extconf.rb"]
  spec.require_paths = ["lib"]

  spec.required_ruby_version = ">= 2.3.0"

  spec.add_development_dependency "bundler", "~> 1.17"
  spec.add_development_dependency "rake", "~> 10.0"
  spec.add_development_dependency "rake-compiler"
end
