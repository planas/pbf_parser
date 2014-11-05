# coding: utf-8
lib = File.expand_path('../lib', __FILE__)
$LOAD_PATH.unshift(lib) unless $LOAD_PATH.include?(lib)

require 'pbf_parser/version'

Gem::Specification.new do |spec|
  spec.name          = "pbf_parser"
  spec.version       = PbfParser::VERSION
  spec.authors       = ["Adrià Planas"]
  spec.email         = ["adriaplanas@liquidcodeworks.com"]
  spec.summary       = %q{Parse Open Street Map PBF files with ease}
  spec.homepage      = "https://github.com/planas/pbf_parser"
  spec.license       = "MIT"

  spec.files         = Dir.glob('lib/**/*.rb') + Dir.glob('ext/**/*.{c,h,rb}')
  spec.extensions    = ['ext/pbf_parser/extconf.rb']
#  spec.test_files    = spec.files.grep(%r{^(test|spec|features)/})
  spec.require_paths = ["lib"]

  spec.add_development_dependency "bundler", "~> 1.3"
  spec.add_development_dependency "rake"
end
