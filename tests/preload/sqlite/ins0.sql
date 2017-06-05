CREATE TABLE Persons(PersonID int, LastName varchar(255), FirstName varchar(255), Address varchar(255), City varchar(255));
CREATE UNIQUE INDEX Persons_PK ON Persons (PersonId);
INSERT INTO Persons (PersonID, LastName, FirstName, Address, City) Values(1,'LN1','FN1','Address1','City1');
INSERT INTO Persons (PersonID, LastName, FirstName, Address, City) Values(2,'LN2','FN2','Address2','City2');
INSERT INTO Persons (PersonID, LastName, FirstName, Address, City) Values(3,'LN3','FN3','Address3','City3');
INSERT INTO Persons (PersonID, LastName, FirstName, Address, City) Values(4,'LN4','FN4','Address4','City4');
