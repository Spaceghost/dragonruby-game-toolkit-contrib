# Build a real, pinned upstream mruby for the test host, not a second game VM.
MRuby::Build.new do |conf|
  toolchain :gcc
  conf.gembox 'default'
  boxing = ENV.fetch('MRUBY_BOXING', 'word')
  definitions = {
    'word' => ['MRB_WORD_BOXING', 'MRB_INT64'],
    'nan' => ['MRB_NAN_BOXING', 'MRB_INT32'],
    'none' => ['MRB_NO_BOXING', 'MRB_INT64']
  }.fetch(boxing)
  conf.cc.defines.concat definitions
  conf.cc.defines << 'MRB_NO_PRESYM'
end
