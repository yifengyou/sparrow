/*
   本文件中的代码只为演示语法,无任何意义,不用深究.
   精力有限,这里只演示部分功能,如果读者感兴趣,
   可以参考core.c中后面注册的原生方法和core.script.inc中的脚本方法
   测试有限,难免还会有bug,读过本书后应该有bugfix的能力.看好你,兄弟.
					     刚子
					     2017.4.12
*/

import employee for Employee
var xh =  Employee.new("xiaohong", "female", 20, 6000)
System.print(xh.salary)

var xm =  Employee.new("xiaoming", "male", 23, 8000)
System.print(xm.salary)

System.print(Employee.employeeNum)

class Manager < Employee {
   var bonus
   bonus=(v) {
      bonus = v 
   }
   
   new(n, g, a, s, b) {
      super(n, g, a, s)
      bonus = b
   }

   salary {
      return super.salary + bonus
   }

}

fun employeeInfo() {
   System.print("number of employee:" + Employee.employeeNum.toString)
   var employeeTitle = Map.new()
   employeeTitle["xh"] = "rd"
   employeeTitle["xm"] = "op"
   employeeTitle["lw"] = "manager"
   employeeTitle["lz"] = "pm"

   for k (employeeTitle.keys) {
      System.print(k + " -> " + employeeTitle[k])
   }

   var employeeHeight = {
      "xh": 170, 
      "xm": 172,
      "lw": 168,
      "lz": 173
   }
   var totalHeight = 0
   for v (employeeHeight.values) {
      totalHeight = totalHeight + v   
   }
   System.print("averageHeight: %(totalHeight / employeeHeight.count)")

   var allEmployee = ["xh", "xm", "lw", "lz"]
   for e (allEmployee) {
      System.print(e)
   }
     
   allEmployee.add("xl")
   System.print("all employee are:%(allEmployee.toString)")
   var idx = 0
   var count = allEmployee.count
   while (idx < count) {
      System.print(allEmployee[idx])
      idx = idx + 1
   }

   //System.gc()  //可以手动回收内存

   var a = 3 + 5 > 9 - 3  ? "yes" : "no"
   if (a.endsWith("s")) {
      System.print(System.clock)
   } else {
      System.print("error!!!!!")
   }

   var str = "hello, world."
   System.print(str[-1..0])
}

var lw = Manager.new("laowang", "male", 35, 13000, 2000)
System.print(lw.salary)
lw.bonus=3100
System.print(lw.salary)
var lz = Manager.new("laozheng", "male", 36, 15000, 2300)
System.print(lz.salary)

var thread = Thread.new(employeeInfo)
thread.call()
