--!nonstrict
local width,height=3440,130
local function draw()
  local w=math.min(922,width)
  barWidget.render(ui.box({key='mina-dock-material',width=width,height=height,fill='#00000000',inputRects={0,0,0,0}}, {
    ui.image({key='dock-hem',path='skin-assets/mina/dock-cloth.png',x=(width-w-104)/2,y=0,width=w,height=height,fit='stretch'}),
  }))
end
function onBarGeometry(w,h) width,height=w,h;draw() end
draw()
