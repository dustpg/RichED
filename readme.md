## Riched-Text Edit Deployer

Cross-platform simplified rich-text edit widget 

跨平台简化富文本编辑控件

![title](./res/title.gif)

### How to use

 - cross-platform

implement your custom ``` RichED::IEDTextPlatform ``` then pass it to ``` RichED::CEDTextDocument ```. you can find demo-code in ```DWnD2D``` for 'DWrite+Direct2D', or ```FTnSDL```  for 'FreeType+SDL'. 

note: demo-code above was not optimized.

 - gui operation funcion

funciton with 'Gui' prefix in ```CEDTextDocument``` is high-level function for GUI operation to meet basic needs,  not for all needs.

 - undo-stack operation

low-level function sucu as ```CEDTextDocument::InsertText``` should be called between ```CEDTextDocument::BeginOP``` and ```CEDTextDocument::EndOP``` to group an undo-stack operation. 'replace text' should be:

```cpp
doc.BeginOP();
doc.RemoveText(begin, end);
doc.InsertText(begin, text);
doc.EndOP();
```

if a low-level function be called out of 'Begin/EndOP', nothing would be recorded on undo-stack.

### 如何使用

 - 跨平台

实现自定义的``` RichED::IEDTextPlatform ```, 并且传给``` RichED::CEDTextDocument ```. 你可以在```DWnD2D```下面找到 DWrite+Direct2D平台, 或者在 ```FTnSDL```下找到FreeType+SDL平台的DEMO代码.

不过请注意: 上述的平台代码仅仅作为例子, 并没有进行优化.

 - Gui操作

```CEDTextDocument```内部会有一些以'Gui'的函数用来满足基本的GUI需求, 不过仅仅是基本的.

 - 撤销栈

一些低级的函数, 比如说 ```CEDTextDocument::InsertText```, 应该被```CEDTextDocument::BeginOP``` 和```CEDTextDocument::EndOP```函数包裹, 用来生成一个撤销栈操作(OP). 比如'替换文本'的操作应该是:

```cpp
doc.BeginOP();
doc.RemoveText(begin, end);
doc.InsertText(begin, text);
doc.EndOP();
```

如果这些低级的函数没有被上述函数包裹, 撤销栈不会记录下来.


### License

  - RichED under MIT License
  - more detail, see [License.txt](./License.txt) 