# 使用方法

1. 把`.vscode`路径下的编译器路径修改为本地环境下的编译器路径；
2. 将需要转换的ttf文件放入到相同路径下；
3. 在main函数中配置好`fontSet`；
4. 将需要转换的字符串放入`text`；
5. 调用`generateBinFile`生成对应的bin文件；
6. 在`readBinFile`和`binFilePath`中填入需要解析的文件名，在`unicode`中填入文化中的某一字符，可以生成对应的log，用来确认bin文件是否正确；
