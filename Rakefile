ME_DIR = File.expand_path('..', __FILE__)

module Utils
  def self.push_cd(dir, &block)
    cwd = Dir.pwd
    Dir.chdir(dir)
    yield
  ensure
    Dir.chdir(cwd) unless cwd.nil?
  end
end

task :clean do
  system("make distclean")
end

task :configure do
  system(
<<EOS
./configure --prefix=#{File.join(ME_DIR, 'out')}
EOS
         )
end

task :make do
  system("make")
end

task :install do
  system("make install")
end

task :build => [ :make, :install ]
task :rebuild => [ :clean, :build ]
task :default => [ :build ]
