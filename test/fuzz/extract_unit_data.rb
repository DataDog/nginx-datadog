def replace_escape_sequences(str)
  str.gsub(/\\r/, "\r")
    .gsub(/\\n/, "\n")
    .gsub(/\\t/, "\t")
    .gsub(/\\\"/, "\"")
    .gsub(/\\\\/, "\\")
end

def extract_sections(input_file)
    re = /parse\("[^"]+",\s*{((".+"[\s\n,]*)+"?)/
    sections = []
    File.open(input_file, 'r') do |file|
        file.read.scan(re) do |match|
            content = match[0].gsub(/^[ ]*"/, '').gsub(/"[,]?(?:\n|$)/, '')
            content = replace_escape_sequences(content)
            sections << content
        end
    end
    sections
end

def write_sections_to_files(sections)
  sections.each_with_index do |section, index|
    filename = "corpus/multipart/   #{index + 1}.txt"
    File.open(filename, 'w') do |file|
      file.write(section)
    end
  end
end

input_file = '../unit/multipart.cpp'
sections = extract_sections(input_file)
write_sections_to_files(sections)
