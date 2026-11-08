--!nonstrict
local actions=require('./actions.luau')
return function(label,glyph,panel)
  barWidget.render(ui.box({key='mina-'..label,width=44,height=30,fill='#00000000'}, {
    ui.glyph({key='icon',name=glyph,x=13,y=6,size=18,color='#E6D8CF'}),
  }))
  barWidget.setTooltip({{key=label,value='Open'}})
  return function() actions.openPanel(panel) end
end
