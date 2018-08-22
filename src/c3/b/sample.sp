import people for People 
fun fn() {
   var p = People.new("xiaoming", "male", 20.0)
   p.sayHi()
}

class Family < People {
   var father
   var mother
   var child
   new(f, m, c) {
      father = f
      mother = m
      child  = c
      super("wbf", "male", 60)
   }
}

var f = Family.new("wbf", "ls", "shine")
f.sayHi()

fn()
