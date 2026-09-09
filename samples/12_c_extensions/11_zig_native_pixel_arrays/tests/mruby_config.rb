# Build a real, pinned upstream mruby for the test host, not a second game VM.
MRuby::Build.new do |conf|
  conf.toolchain :gcc
  # Keep the build graph and compiler configuration in agreement, including mrbc.
  conf.disable_presym
  boxing = ENV.fetch('MRUBY_BOXING', 'word')
  definitions = {
    'word' => ['MRB_WORD_BOXING', 'MRB_INT64'],
    'nan' => ['MRB_NAN_BOXING', 'MRB_INT32'],
    'none' => ['MRB_NO_BOXING', 'MRB_INT64']
  }.fetch(boxing)
  conf.cc.defines.concat definitions
  # Configure the VM before gems inherit its compiler options.
  conf.gembox 'default'
end
