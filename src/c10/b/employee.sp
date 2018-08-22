class Employee {
   var name
   var gender
   var age
   var salary
   static var employeeNum = 0
   new(n, g, a, s) {
      name = n
      gender = g
      age = a
      salary = s
      employeeNum = employeeNum + 1
   }

   sayHi() {
      System.print("My name is " + 
	    name + ", I am a " + gender + 
	    ", " + age.toString + "years old") 
   }

   salary {
      return salary
   }

   static employeeNum {
      return employeeNum
   }
}
